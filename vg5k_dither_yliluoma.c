/* ============================================================================
 * ditherTo8ColorsY.c - traduction C de ditherTo8ColorsY.py (Vg5kGraphics)
 * ----------------------------------------------------------------------------
 * Algorithme de tramage ordonne "arbitrary-palette positional dithering"
 * de Joel Yliluoma : https://bisqwit.iki.fi/story/howto/dither/jy/
 *
 * Entree : image RGB chargee par stb_image (nom de fichier en argument 1).
 * Sortie : image d indices de palette, 1 octet par pixel, indices dans
 *          pal (0 blanc, 1 cyan, 2 magenta, 3 bleu, 4 jaune, 5 vert,
 *          6 rouge, 7 noir - meme convention que le VG5000), ecrite
 *          dans OUTPUT_IDX. Un apercu PNG (OUTPUT_PNG) est aussi genere
 *          pour controle visuel.
 *
 * Tous les parametres de l'outil Python deviennent des #define ci dessous.
 * Compilation (cote PC) :
 *   gcc -O2 -o dither8 ditherTo8ColorsY.c -lm
 *   ./dither8 image.png
 *
 * Differences assumees par rapport au script Python :
 *   - le membre C du LabItem : le script contient une coquille
 *     "ct = a*a + b + b" ; ici on retablit "ct = a*a + b*b" (CIEDE2000
 *     seulement, modes 6/7 avec USE_CIEDE2000) ;
 *   - l arrondi de PIL (point) est approxime par (int)(x + 0.5) ;
 *   - l ordre du dict Python (algo 3) est remplace par l ordre des
 *     indices de palette (meme ensemble de couleurs, ordre interne
 *     different, sans effet apres le tri par luminance) ;
 *   - les divisions // de Python (vers -infini) passent par floordiv().
 * ==========================================================================*/

#include "vg5k_dither_yliluoma.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Parametres (anciens arguments de la ligne de commande du script)    */
/* ------------------------------------------------------------------ */

/* Mode = -m du script Python :
 *  0 = algorithme 1a (exhaustif, difference RGB seule)
 *  1 = algorithme 1ba (exhaustif, + penalite de mixage 10 %)
 *  2 = algorithme 1bb (exhaustif, CCIR 601)
 *  3 = plus rapide (ratio estime, CCIR 601)
 *  4 = tri-tone (4 couleurs en motif 2x2)
 *  5 = algorithme 2 (melange de n couleurs)
 *  6 = algorithme 2 avec correction gamma
 *  7 = algorithme 3 (subdivision recursive)
 *  8 = tramage facon adobe (defaut, comme main() du script Python) */
#define MODE 8

/* Matrice de seuil = -d du script : 24 = 4x2, 48 = 8x4 */
#define DITHER_MAP 24

/* Pre-correction gamma de l'image source (comme im.point du main Python) :
 *  0.8 eclaircit, 1.2 assombrit, 1.0 = aucune correction */
#define INPUT_GAMMA 1.0

/* CIEDE2000 pour les modes 6 et 7 (-c du script) */
#define USE_CIEDE2000 0

#define OUTPUT_IDX "im_ordered.idx" /* indices bruts w*h octets */
#define OUTPUT_PNG "im_ordered.png" /* apercu RGB via la palette */

/* ------------------------------------------------------------------ */
/* Palette (pal du script Python) : 0 blanc ... 7 noir, VG5000        */
/* ------------------------------------------------------------------ */

#define N_COLORS 8

static const int pal[N_COLORS][3] = {
    {255, 255, 255}, /* 0 blanc   */
    {0, 255, 255},   /* 1 cyan     */
    {255, 0, 255},   /* 2 magenta  */
    {0, 0, 255},     /* 3 bleu     */
    {255, 255, 0},   /* 4 jaune    */
    {0, 255, 0},     /* 5 vert     */
    {255, 0, 0},     /* 6 rouge    */
    {0, 0, 0}        /* 7 noir     */
};

/* ------------------------------------------------------------------ */
/* Matrices de seuil (dithermaps du script Python)                    */
/* ------------------------------------------------------------------ */

#if DITHER_MAP == 24
static const int dmap[4][2] = {{0, 3}, {4, 7}, {2, 1}, {6, 5}};
#define DMAP_H 4
#define DMAP_W 2
#define D_RANGE 8
#elif DITHER_MAP == 48
static const int dmap[8][4] = {{0, 12, 3, 15}, {16, 28, 19, 31}, {8, 4, 11, 7}, {24, 20, 27, 23},
                               {2, 14, 1, 13}, {18, 30, 17, 29}, {10, 6, 9, 5}, {26, 22, 25, 21}};
#define DMAP_H 8
#define DMAP_W 4
#define D_RANGE 32
#else
#error "DITHER_MAP doit valoir 24 ou 48"
#endif

/* Taille max d un plan de mixage : l algo 3 peut produire jusqu a
 *  N_COLORS * limit entrees aples arrondis vers le haut des comptes
 *  fractionnaires ; les autres modes produisent au plus max(N_COLORS, D_RANGE). */
#define MAX_PLAN (N_COLORS * N_COLORS)

/* ------------------------------------------------------------------ */
/* Tables derivees : luminance et palette gamma-corrigee              */
/* ------------------------------------------------------------------ */

static int luma[N_COLORS];        /* r*299 + g*587 + b*114          */
static double pal_g[N_COLORS][3]; /* palette dans l espace gamma 2.2 */

static const double gamma_value = 2.2; /* gamma interne de l algo */

static double gamma_correct(double v)
{
    return pow(v, gamma_value);
}
static double gamma_uncorrect(double v)
{
    return pow(v, 1.0 / gamma_value);
}

static void init_tables(void)
{
    int i;
    for (i = 0; i < N_COLORS; i++)
    {
        luma[i] = pal[i][0] * 299 + pal[i][1] * 587 + pal[i][2] * 114;
        pal_g[i][0] = gamma_correct(pal[i][0] / 255.0);
        pal_g[i][1] = gamma_correct(pal[i][1] / 255.0);
        pal_g[i][2] = gamma_correct(pal[i][2] / 255.0);
    }
}

/* Division entiere vers -infini (equivalent du // Python), b > 0 */
static int floordiv(int a, int b)
{
    return (a >= 0) ? a / b : -((-a + b - 1) / b);
}

/* ------------------------------------------------------------------ */
/* Comparaison de couleurs                                            */
/* ------------------------------------------------------------------ */

/* Difference carree de deux RGB (0-255) */
static double color_compare(double r1, double g1, double b1, double r2, double g2, double b2)
{
    double r = (r1 - r2) / 255.0;
    double g = (g1 - g2) / 255.0;
    double b = (b1 - b2) / 255.0;
    return r * r + g * g + b * b;
}

/* Idem, ponderee par la luminosite CCIR 601 */
static double color_compare_ccir601(double r1, double g1, double b1, double r2, double g2, double b2)
{
    double luma1 = (r1 * 299 + g1 * 587 + b1 * 114) / (255.0 * 1000.0);
    double luma2 = (r2 * 299 + g2 * 587 + b2 * 114) / (255.0 * 1000.0);
    double lumad = luma1 - luma2;
    double r = (r1 - r2) / 255.0;
    double g = (g1 - g2) / 255.0;
    double b = (b1 - b2) / 255.0;
    return (r * r * 0.299 + g * g * 0.587 + b * b * 0.114) * 0.75 + lumad * lumad;
}

/* ------------------------------------------------------------------ */
/* CIE L*a*b* (illuminant C)                                          */
/* ------------------------------------------------------------------ */

typedef struct
{
    double lum, a, b, c, h;
} LabItem;

/* CIE C illuminant (illum du script Python) */
static const double illum[9] = {0.488718,  0.176204, 0.000000,  0.310680, 0.812985,
                                0.0102048, 0.200602, 0.0108109, 0.989795};

static void lab_set(LabItem *lab, double r, double g, double b)
{
    double xx = illum[0] * r + illum[3] * g + illum[6] * b;
    double yy = illum[1] * r + illum[4] * g + illum[7] * b;
    double zz = illum[2] * r + illum[5] * g + illum[8] * b;
    double x = xx / (illum[0] + illum[1] + illum[2]);
    double y = yy / (illum[3] + illum[4] + illum[5]);
    double z = zz / (illum[6] + illum[7] + illum[8]);
    const double t1 = (6 * 6 * 6.0) / (29 * 29 * 29.0);
    const double t2 = (29 * 29.0) / (6 * 6 * 3.0);
    double x1 = (x > t1) ? pow(x, 1.0 / 3.0) : t2 * x + 4.0 / 29.0;
    double y1 = (y > t1) ? pow(y, 1.0 / 3.0) : t2 * y + 4.0 / 29.0;
    double z1 = (z > t1) ? pow(z, 1.0 / 3.0) : t2 * z + 4.0 / 29.0;
    double ct;

    lab->lum = (29 * 4) * y1 - (4 * 4);
    lab->a = 500 * (x1 - y1);
    lab->b = 200 * (y1 - z1);

    /* le script Python contient ici la coquille "a*a + b + b" ;
     *      on retablit la formule CIE "a*a + b*b" */
    ct = lab->a * lab->a + lab->b * lab->b;
    if (ct < 0)
        ct = 0;
    lab->c = sqrt(ct);
    lab->h = atan2(lab->b, lab->a);
}

/* ------------------------------------------------------------------ */
/* CIEDE2000 (color_compare_ciede2000 du script Python)              */
/* ------------------------------------------------------------------ */

#define RAD(x) ((x) * M_PI / 180.0)
#define DEG(x) ((x) * 180.0 / M_PI)

static double color_compare_ciede2000(const LabItem *lab1, const LabItem *lab2)
{
    double cab = 0.5 * (lab1->c + lab2->c);
    double cab7 = pow(cab, 7.0);
    double g = 0.5 * (1.0 - sqrt(cab7 / (cab7 + 6103515625.0)));
    double a1 = (1.0 + g) * lab1->a;
    double a2 = (1.0 + g) * lab2->a;
    double c1 = sqrt(a1 * a1 + lab1->b * lab1->b);
    double c2 = sqrt(a2 * a2 + lab2->b * lab2->b);
    double h1 = 0.0, h2 = 0.0, dhh = 0.0, dh;
    double lum, c, h, t, hh, ddeg, c7, rc, l50sq, sl, sc, sh, rt;
    double dl, dc, dlsq, dcsq, dhsq;

    if (c1 >= 1e-9)
    {
        h1 = DEG(atan2(lab1->b, a1));
        if (h1 < 0.0)
            h1 += 360.0;
    }
    if (c2 >= 1e-9)
    {
        h2 = DEG(atan2(lab2->b, a2));
        if (h2 < 0.0)
            h2 += 360.0;
    }

    dl = lab2->lum - lab1->lum;
    dc = c2 - c1;

    if (c1 >= 1e-9 && c2 >= 1e-9)
    {
        dhh = h2 - h1;
        if (dhh > 180.0)
            dhh -= 360.0;
        else if (dhh < -180.0)
            dhh += 360.0;
    }
    dh = 2.0 * sqrt(c1 * c2) * sin(RAD(0.5 * dhh));

    lum = 0.5 * (lab1->lum + lab2->lum);
    c = 0.5 * (c1 + c2);
    if (c1 < 1e-9 || c2 < 1e-9)
    {
        h = h1 + h2;
    }
    else
    {
        h = h1 + h2;
        if (fabs(h1 - h2) > 180.0)
        {
            if (h < 360.0)
                h += 360.0;
            else if (h >= 360.0)
                h -= 360.0;
        }
        h *= 0.5;
    }

    t = 1.0 - 0.17 * cos(RAD(h - 30.0)) + 0.24 * cos(RAD(2.0 * h)) + 0.32 * cos(RAD(3.0 * h + 6.0)) -
        0.20 * cos(RAD(4.0 * h - 63.0));

    hh = (h - 275.0) / 25.0;
    ddeg = 30.0 * exp(-hh * hh);
    c7 = pow(c, 7.0);
    rc = 2.0 * sqrt(c7 / (c7 + 6103515625.0));
    l50sq = (lum - 50.0) * (lum - 50.0);
    sl = 1.0 + (0.015 * l50sq) / sqrt(20.0 + l50sq);
    sc = 1.0 + 0.045 * c;
    sh = 1.0 + 0.015 * c * t;
    rt = -sin(RAD(2.0 * ddeg)) * rc;
    dlsq = dl / sl;
    dcsq = dc / sc;
    dhsq = dh / sh;

    return dlsq * dlsq + dcsq * dcsq + dhsq * dhsq + rt * dcsq * dhsq;
}

/* ------------------------------------------------------------------ */
/* Erreur d'un melange (evaluate_mixing_error du script Python)       */
/* ------------------------------------------------------------------ */

static double evaluate_mixing_error(int r, int g, int b, double r0, double g0, double b0, int r1, int g1, int b1,
                                    int r2, int g2, int b2, double ratio, int mode)
{
    if (mode == 0)
    {
        /* 0 : algorithme 1a */
        return color_compare(r, g, b, r0, g0, b0);
    }
    else if (mode == 1)
    {
        /* 1 : algorithme 1ba */
        double cc0 = color_compare(r, g, b, r0, g0, b0);
        double cc1 = color_compare(r1, g1, b1, r2, g2, b2);
        return cc0 + cc1 * 0.1;
    }
    else
    {
        /* 2, 3, 4 : ponderees CCIR 601 */
        double cc0 = color_compare_ccir601(r, g, b, r0, g0, b0);
        double cc1 = color_compare_ccir601(r1, g1, b1, r2, g2, b2);
        return cc0 + cc1 * 0.1 * (fabs(ratio - 0.5) + 0.5);
    }
}

/* ------------------------------------------------------------------ */
/* Tri stable par luminance (equivalent de sorted(key=luma[x]))       */
/* ------------------------------------------------------------------ */

static void sort_by_luma(int *cols, int len)
{
    int i, j;
    for (i = 1; i < len; i++)
    {
        int v = cols[i];
        double lv = luma[v];
        for (j = i - 1; j >= 0 && luma[cols[j]] > lv; j--)
            cols[j + 1] = cols[j];
        cols[j + 1] = v;
    }
}

/* ------------------------------------------------------------------ */
/* Algorithmes 1a / 1ba / 1bb (modes 0-2) : exhaustive               */
/* ------------------------------------------------------------------ */

static void devise_best_mixing_plan(const int col[3], int mode, int r_colors[2], double *r_ratio)
{
    int index1, index2, ratio;
    double least_penalty = 1e99;

    r_colors[0] = 0;
    r_colors[1] = 0;
    *r_ratio = 0.5;

    for (index1 = 0; index1 < N_COLORS; index1++)
    {
        int r1 = pal[index1][0], g1 = pal[index1][1], b1 = pal[index1][2];
        for (index2 = index1; index2 < N_COLORS; index2++)
        {
            int r2 = pal[index2][0], g2 = pal[index2][1], b2 = pal[index2][2];
            for (ratio = 0; ratio < D_RANGE; ratio++)
            {
                double r0, g0, b0, penalty;
                if (index1 == index2 && ratio != 0)
                    break;
                r0 = r1 + floordiv(ratio * (r2 - r1), D_RANGE);
                g0 = g1 + floordiv(ratio * (g2 - g1), D_RANGE);
                b0 = b1 + floordiv(ratio * (b2 - b1), D_RANGE);
                penalty = evaluate_mixing_error(col[0], col[1], col[2], r0, g0, b0, r1, g1, b1, r2, g2, b2,
                                                (double)ratio / D_RANGE, mode);
                if (penalty < least_penalty)
                {
                    least_penalty = penalty;
                    r_colors[0] = index1;
                    r_colors[1] = index2;
                    *r_ratio = (double)ratio / D_RANGE;
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Mode 3 : ratio estime (devise_best_mixing_plan_fast)               */
/* ------------------------------------------------------------------ */

static void devise_best_mixing_plan_fast(const int col[3], int r_colors[2], double *r_ratio)
{
    int index1, index2;
    double least_penalty = 1e99;

    r_colors[0] = 0;
    r_colors[1] = 0;
    *r_ratio = 0.5;

    for (index1 = 0; index1 < N_COLORS; index1++)
    {
        int r1 = pal[index1][0], g1 = pal[index1][1], b1 = pal[index1][2];
        for (index2 = index1; index2 < N_COLORS; index2++)
        {
            int r2 = pal[index2][0], g2 = pal[index2][1], b2 = pal[index2][2];
            double ratio = D_RANGE / 2.0;
            double r0, g0, b0, penalty;

            if (r1 != r2 || g1 != g2 || b1 != b2)
            {
                /* ratio optimal estime canal par canal, pondere CCIR 601 :
                 *                  resoudre r1 + ratio*(r2-r1)/d_range = r               */
                double rr = 0, gg = 0, bb = 0;
                int rd = 0, gd = 0, bd = 0;

                if (r2 != r1)
                {
                    rr = 299.0 * D_RANGE * (col[0] - r1) / (r2 - r1);
                    rd = 299;
                }
                if (g2 != g1)
                {
                    gg = 587.0 * D_RANGE * (col[1] - g1) / (g2 - g1);
                    gd = 587;
                }
                if (b2 != b1)
                {
                    bb = 114.0 * D_RANGE * (col[2] - b1) / (b2 - b1);
                    bd = 114;
                }

                ratio = (rr + gg + bb) / (rd + gd + bd);
                if (ratio < 0.0)
                    ratio = 0.0;
                else if (ratio > D_RANGE - 1)
                    ratio = D_RANGE - 1;
            }

            r0 = r1 + ratio * (r2 - r1) / D_RANGE;
            g0 = g1 + ratio * (g2 - g1) / D_RANGE;
            b0 = b1 + ratio * (b2 - b1) / D_RANGE;

            penalty =
                evaluate_mixing_error(col[0], col[1], col[2], r0, g0, b0, r1, g1, b1, r2, g2, b2, ratio / D_RANGE, 2);
            if (penalty < least_penalty)
            {
                least_penalty = penalty;
                r_colors[0] = index1;
                r_colors[1] = index2;
                *r_ratio = ratio / D_RANGE;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Mode 4 : tri-tone (devise_best_mixing_plan_tritone)                */
/* ratio == 4.0 en retour => r_colors est un motif 2x2 :              */
/*   r_colors[0] = (0,0) = (1,1) = index3, r_colors[1] = (0,1) = index1 */
/*   r_colors[2] = (1,0) = index2                                      */
/* ------------------------------------------------------------------ */

static void devise_best_mixing_plan_tritone(const int col[3], int r_colors[4], double *r_ratio)
{
    int index1, index2, index3;
    double least_penalty = 1e99;

    r_colors[0] = 0;
    r_colors[1] = 0;
    r_colors[2] = 0;
    r_colors[3] = 0;
    *r_ratio = 0.5;

    for (index1 = 0; index1 < N_COLORS; index1++)
    {
        int r1 = pal[index1][0], g1 = pal[index1][1], b1 = pal[index1][2];
        for (index2 = index1; index2 < N_COLORS; index2++)
        {
            int r2 = pal[index2][0], g2 = pal[index2][1], b2 = pal[index2][2];
            double ratio = D_RANGE / 2.0;
            double r0, g0, b0, penalty;

            if (r1 != r2 || g1 != g2 || b1 != b2)
            {
                double rr = 0, gg = 0, bb = 0;
                int rd = 0, gd = 0, bd = 0;

                if (r2 != r1)
                {
                    rr = 299.0 * D_RANGE * (col[0] - r1) / (r2 - r1);
                    rd = 299;
                }
                if (g2 != g1)
                {
                    gg = 587.0 * D_RANGE * (col[1] - g1) / (g2 - g1);
                    gd = 587;
                }
                if (b2 != b1)
                {
                    bb = 114.0 * D_RANGE * (col[2] - b1) / (b2 - b1);
                    bd = 114;
                }

                ratio = (rr + gg + bb) / (rd + gd + bd);
                if (ratio < 0.0)
                    ratio = 0.0;
                else if (ratio > D_RANGE - 1)
                    ratio = D_RANGE - 1;
            }

            r0 = r1 + ratio * (r2 - r1) / D_RANGE;
            g0 = g1 + ratio * (g2 - g1) / D_RANGE;
            b0 = b1 + ratio * (b2 - b1) / D_RANGE;

            penalty =
                evaluate_mixing_error(col[0], col[1], col[2], r0, g0, b0, r1, g1, b1, r2, g2, b2, ratio / D_RANGE, 2);
            if (penalty < least_penalty)
            {
                least_penalty = penalty;
                r_colors[0] = index1;
                r_colors[1] = index2;
                *r_ratio = ratio / D_RANGE;
            }

            if (index1 == index2)
                continue;

            /* troisieme couleur : 50 % index3, 25 % index1, 25 % index2 */
            for (index3 = 0; index3 < N_COLORS; index3++)
            {
                int r3 = pal[index3][0], g3 = pal[index3][1], b3 = pal[index3][2];
                double cc0, cc1, cc2;

                if (index3 == index2 || index3 == index1)
                    continue;

                r0 = (r1 + r2 + r3 * 2.0) / 4.0;
                g0 = (g1 + g2 + g3 * 2.0) / 4.0;
                b0 = (b1 + b2 + b3 * 2.0) / 4.0;

                cc0 = color_compare_ccir601(col[0], col[1], col[2], r0, g0, b0);
                cc1 = color_compare_ccir601(r1, g1, b1, r2, g2, b2);
                cc2 = color_compare_ccir601((r1 + r2) / 2.0, (g1 + g2) / 2.0, (b1 + b2) / 2.0, r3, g3, b3);

                penalty = cc0 + cc1 * 0.025 + cc2 * 0.025;
                if (penalty < least_penalty)
                {
                    least_penalty = penalty;
                    r_colors[0] = index3; /* (0,0) et (1,1) */
                    r_colors[1] = index1; /* (0,1)          */
                    r_colors[2] = index2; /* (1,0)          */
                    r_colors[3] = index3;
                    *r_ratio = 4.0;
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Mode 5 : algorithme 2 (devise_best_mixing_plan2)                   */
/* Construit un melange de n_colors couleurs approchant col.          */
/* ------------------------------------------------------------------ */

static void devise_best_mixing_plan2(const int col[3], int *cols, int n_colors)
{
    int proportion_total = 0;
    double so_far[3] = {0.0, 0.0, 0.0};
    int chosen = 0, chosen_amount = 1;

    while (proportion_total < n_colors)
    {
        int max_test_count = (proportion_total > 1) ? proportion_total : 1;
        double least_penalty = -1.0;
        int index, p, c, i;

        chosen_amount = 1;
        chosen = 0;

        for (index = 0; index < n_colors; index++)
        {
            double sum[3], add[3];
            sum[0] = so_far[0];
            sum[1] = so_far[1];
            sum[2] = so_far[2];
            add[0] = pal[index][0];
            add[1] = pal[index][1];
            add[2] = pal[index][2];

            for (p = 1; p <= max_test_count; p *= 2)
            {
                double test[3], penalty;
                int t = proportion_total + p;
                for (c = 0; c < 3; c++)
                {
                    sum[c] += add[c];
                    add[c] += add[c];
                }
                test[0] = sum[0] / t;
                test[1] = sum[1] / t;
                test[2] = sum[2] / t;
                penalty = color_compare_ccir601(col[0], col[1], col[2], test[0], test[1], test[2]);
                if (penalty < least_penalty || least_penalty < 0.0)
                {
                    least_penalty = penalty;
                    chosen = index;
                    chosen_amount = p;
                }
            }
        }

        for (i = 0; i < chosen_amount; i++)
        {
            if (proportion_total >= n_colors)
                break;
            cols[proportion_total] = chosen;
            proportion_total++;
        }
        so_far[0] += pal[chosen][0] * chosen_amount;
        so_far[1] += pal[chosen][1] * chosen_amount;
        so_far[2] += pal[chosen][2] * chosen_amount;
    }

    sort_by_luma(cols, n_colors);
}

/* ------------------------------------------------------------------ */
/* Mode 6 : algorithme 2, espace gamma (devise_best_mixing_plan2g)    */
/* Renvoie le nombre d entrees de cols (peut depasser limit si le    */
/* dernier chosen_amount depasse la place restante, comme en Python). */
/* ------------------------------------------------------------------ */

static int devise_best_mixing_plan2g(const int col[3], int *cols, int n_colors, int limit, int use_ciede2000)
{
    LabItem inputlab;
    double so_far[3] = {0.0, 0.0, 0.0};
    int len = 0;

    lab_set(&inputlab, col[0] / 255.0, col[1] / 255.0, col[2] / 255.0);

    while (len < limit)
    {
        int max_test_count = (len == 0) ? 1 : len;
        double least_penalty = -1.0;
        int chosen = 0, chosen_amount = 1;
        int index, p, c;

        for (index = 0; index < n_colors; index++)
        {
            double sum[3], add[3];
            sum[0] = so_far[0];
            sum[1] = so_far[1];
            sum[2] = so_far[2];
            add[0] = pal_g[index][0];
            add[1] = pal_g[index][1];
            add[2] = pal_g[index][2];

            for (p = 1; p <= max_test_count; p *= 2)
            {
                double test[3], penalty;
                int t = len + p;
                for (c = 0; c < 3; c++)
                {
                    sum[c] += add[c];
                    add[c] += add[c];
                }
                test[0] = gamma_uncorrect(sum[0] / t);
                test[1] = gamma_uncorrect(sum[1] / t);
                test[2] = gamma_uncorrect(sum[2] / t);

                if (use_ciede2000)
                {
                    LabItem test_lab;
                    lab_set(&test_lab, test[0], test[1], test[2]);
                    penalty = color_compare_ciede2000(&test_lab, &inputlab);
                }
                else
                {
                    penalty = color_compare_ccir601(col[0], col[1], col[2], test[0] * 255.0, test[1] * 255.0,
                                                    test[2] * 255.0);
                }
                if (penalty < least_penalty || least_penalty < 0.0)
                {
                    least_penalty = penalty;
                    chosen = index;
                    chosen_amount = p;
                }
            }
        }

        /* le plan peut depasser limit (comportement du script Python) */
        {
            int cnt = len + chosen_amount;
            while (len < cnt)
                cols[len++] = chosen;
        }
        so_far[0] += pal_g[chosen][0] * chosen_amount;
        so_far[1] += pal_g[chosen][1] * chosen_amount;
        so_far[2] += pal_g[chosen][2] * chosen_amount;
    }

    sort_by_luma(cols, len);
    return len;
}

/* ------------------------------------------------------------------ */
/* Mode 7 : algorithme 3 (devise_best_mixing_plan3)                   */
/* Subdivision recursive d une solution initiale monochrome.          */
/* Renvoie le nombre d entrees de cols.                               */
/* ------------------------------------------------------------------ */

static int devise_best_mixing_plan3(const int col[3], int *cols, int n_colors, int limit, int use_ciede2000)
{
    LabItem inputlab;
    double solution[N_COLORS];
    double current_penalty = -1.0;
    double dbllimit;
    int chosen = 0, index, len = 0, i;

    lab_set(&inputlab, col[0] / 255.0, col[1] / 255.0, col[2] / 255.0);

    /* graine : la couleur la plus proche de la couleur demandee */
    for (index = 0; index < n_colors; index++)
    {
        double penalty;
        int cr = pal[index][0], cg = pal[index][1], cb = pal[index][2];
        if (use_ciede2000)
        {
            LabItem test_lab;
            lab_set(&test_lab, cr / 255.0, cg / 255.0, cb / 255.0);
            penalty = color_compare_ciede2000(&inputlab, &test_lab);
        }
        else
        {
            penalty = color_compare_ccir601(col[0], col[1], col[2], cr, cg, cb);
        }
        if (penalty < current_penalty || current_penalty < 0.0)
        {
            current_penalty = penalty;
            chosen = index;
        }
    }

    memset(solution, 0, sizeof(solution));
    solution[chosen] = (double)limit;

    dbllimit = 1.0 / limit;

    while (current_penalty != 0.0)
    {
        double best_penalty = current_penalty;
        int best_splitfrom = -1, best_a = 0, best_b = 0;
        int split_color;

        for (split_color = 0; split_color < n_colors; split_color++)
        {
            double split_count = solution[split_color];
            double sum[3] = {0.0, 0.0, 0.0};
            double portion1, portion2;
            int a, b, other;

            if (split_count <= 0.0)
                continue;

            /* cumul des autres couleurs */
            for (other = 0; other < n_colors; other++)
            {
                if (other == split_color)
                    continue;
                sum[0] += pal_g[other][0] * solution[other] * dbllimit;
                sum[1] += pal_g[other][1] * solution[other] * dbllimit;
                sum[2] += pal_g[other][2] * solution[other] * dbllimit;
            }

            portion1 = (split_count / 2.0) * dbllimit;
            portion2 = (split_count - split_count / 2.0) * dbllimit;

            for (a = 0; a < n_colors; a++)
            {
                int firstb = (portion1 == portion2) ? a + 1 : 0;
                for (b = firstb; b < n_colors; b++)
                {
                    double test[3], penalty, lumadiff;
                    if (a == b)
                        continue;
                    lumadiff = luma[a] - luma[b];
                    if (lumadiff < 0)
                        lumadiff = -lumadiff;
                    if (lumadiff > 80000)
                        continue;

                    test[0] = gamma_uncorrect(sum[0] + pal_g[a][0] * portion1 + pal_g[b][0] * portion2);
                    test[1] = gamma_uncorrect(sum[1] + pal_g[a][1] * portion1 + pal_g[b][1] * portion2);
                    test[2] = gamma_uncorrect(sum[2] + pal_g[a][2] * portion1 + pal_g[b][2] * portion2);

                    if (use_ciede2000)
                    {
                        LabItem test_lab;
                        lab_set(&test_lab, test[0], test[1], test[2]);
                        penalty = color_compare_ciede2000(&inputlab, &test_lab);
                    }
                    else
                    {
                        penalty = color_compare_ccir601(col[0], col[1], col[2], test[0] * 255.0, test[1] * 255.0,
                                                        test[2] * 255.0);
                    }

                    if (penalty < best_penalty)
                    {
                        best_penalty = penalty;
                        best_splitfrom = split_color;
                        best_a = a;
                        best_b = b;
                    }
                    if (portion2 == 0.0)
                        break;
                }
            }
        }

        if (best_splitfrom < 0)
            break; /* pas d amelioration : comme best_penalty == current_penalty */

        {
            double split_count = solution[best_splitfrom];
            double split1 = split_count / 2.0;
            double split2 = split_count - split1;
            solution[best_splitfrom] = 0.0; /* del solution[...] */
            if (split1 > 0.0)
                solution[best_a] += split1;
            if (split2 > 0.0)
                solution[best_b] += split2;
        }
        current_penalty = best_penalty;
    }

    /* mise en sequence : chaque couleur contribue ceil(count) entrees
     *      (equivalent du while len < len + cnt du Python) */
    for (index = 0; index < n_colors; index++)
    {
        double cnt = solution[index];
        int n = (int)ceil(cnt);
        for (i = 0; i < n && len < MAX_PLAN; i++)
            cols[len++] = index;
    }

    sort_by_luma(cols, len);
    return len;
}

/* ------------------------------------------------------------------ */
/* Mode 8 : tramage facon adobe (devise_best_mixing_plan4)            */
/* d_range couleurs, accumulation d'erreur locale au pixel.           */
/* ------------------------------------------------------------------ */

static void devise_best_mixing_plan4(const int col[3], int *cols, int n_colors, int d_range)
{
    const double lx = 0.09; /* multiplicateur d erreur */
    double e[3] = {0.0, 0.0, 0.0};
    int c;

    for (c = 0; c < d_range; c++)
    {
        int t[3], k, index, chosen = c % n_colors;
        double least_penalty = 1e99;

        for (k = 0; k < 3; k++)
        {
            int v = (int)(col[k] + e[k] * lx);
            if (v < 0)
                v = 0;
            else if (v > 255)
                v = 255;
            t[k] = v;
        }

        for (index = 0; index < n_colors; index++)
        {
            double penalty = color_compare_ccir601(pal[index][0], pal[index][1], pal[index][2], t[0], t[1], t[2]);
            if (penalty < least_penalty)
            {
                least_penalty = penalty;
                chosen = index;
            }
        }

        cols[c] = chosen;
        e[0] += col[0] - pal[chosen][0];
        e[1] += col[1] - pal[chosen][1];
        e[2] += col[2] - pal[chosen][2];
    }

    sort_by_luma(cols, d_range);
}

/* ------------------------------------------------------------------ */
/* Tramage principal (convert_dither du script Python)               */
/* ------------------------------------------------------------------ */

/* Entree : src = image RGB (3 octets par pixel, w * h pixels),
 *  eventuellement pre-corrigee gamma.
 *  Sortie : buffer malloc de w * h octets, indices 0-7 dans pal. */

unsigned char *dither_y(unsigned char *src, int w, int h)
{
    init_tables();
    unsigned char *out = (unsigned char *)malloc((size_t)w * h);
    int x, y;

    if (out == NULL)
        return NULL;

    for (y = 0; y < h; y++)
    {
        for (x = 0; x < w; x++)
        {
            const unsigned char *px = &src[(y * w + x) * 4];
            int col[3];
            int mv = dmap[y % DMAP_H][x % DMAP_W];
            int idx = 0;

            col[0] = px[0];
            col[1] = px[1];
            col[2] = px[2];

            switch (MODE)
            {
            case 0:
            case 1:
            case 2: {
                int cc[2];
                double ratio;
                devise_best_mixing_plan(col, MODE, cc, &ratio);
                if ((double)mv / D_RANGE < ratio)
                    idx = cc[1];
                else
                    idx = cc[0];
                break;
            }
            case 3: {
                int cc[2];
                double ratio;
                devise_best_mixing_plan_fast(col, cc, &ratio);
                if ((double)mv / D_RANGE < ratio)
                    idx = cc[1];
                else
                    idx = cc[0];
                break;
            }
            case 4: {
                int cc[4];
                double ratio;
                devise_best_mixing_plan_tritone(col, cc, &ratio);
                if (ratio == 4.0)
                    idx = cc[(y % 2) * 2 + (x % 2)];
                else if ((double)mv / D_RANGE < ratio)
                    idx = cc[1];
                else
                    idx = cc[0];
                break;
            }
            case 5: {
                int cols[MAX_PLAN];
                devise_best_mixing_plan2(col, cols, N_COLORS);
                idx = cols[mv * N_COLORS / D_RANGE];
                break;
            }
            case 6: {
                int cols[MAX_PLAN];
                int len = devise_best_mixing_plan2g(col, cols, N_COLORS, N_COLORS, USE_CIEDE2000);
                idx = cols[mv * len / D_RANGE];
                break;
            }
            case 7: {
                int cols[MAX_PLAN];
                int len = devise_best_mixing_plan3(col, cols, N_COLORS, N_COLORS, USE_CIEDE2000);
                idx = cols[mv * len / D_RANGE];
                break;
            }
            default:
            case 8: {
                int cols[MAX_PLAN];
                devise_best_mixing_plan4(col, cols, N_COLORS, D_RANGE);
                idx = cols[mv];
                break;
            }
            }

            out[y * w + x] = (unsigned char)idx;
        }
    }

    // unsigned char *image = malloc(w * h * 4);
    for (int i = 0, j = 0; i < w * h * 4; i += 4, j++)
    {
        unsigned char pal_idx = out[j];
        // printf("pal_idx:%d\n", pal_idx);
        src[i] = pal[pal_idx][0];         // Rouge
        src[i + 1] = pal[pal_idx][1]; // Vert
        src[i + 2] = pal[pal_idx][2]; // Bleu
        src[i + 3] = 0xFF; // Alpha
    }


    return out;
}
