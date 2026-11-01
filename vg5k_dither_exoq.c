/* ============================================================================
 * vg5k_dither.c - implementation de vg5k_dither.h
 * ----------------------------------------------------------------------------
 * Compilation (cote PC, generateur d assets) :
 *   gcc -O2 -c vg5k_dither.c -o vg5k_dither.o
 * ==========================================================================*/

#include "vg5k_dither_exoq.h"

#include <stdlib.h>

/* ---------------------------------------------------------------------------
 * Palette materielle (RGBA) et son equivalent dans l espace couleur de
 * travail : espace GAMMA (valeurs sRGB brutes, SANS linearisation),
 * avec les ponderations d exoquant (SCALE_R = 1.0, SCALE_G = 1.2,
 * SCALE_B = 0.8).
 *
 * Pourquoi PAS la linearisation sRGB : avec une palette dure de 8
 * primaires, l espace lineaire ecrase les demi-tons (un gris 50 %
 * lineaire vaut ~0.22, quatre fois plus proche du noir que du
 * blanc : tout snappe vers le sombre, contraste exagere). L espace
 * gamma, plus proche de la perception visuelle, met un gris 50 % a
 * equidistance du noir et du blanc -> les demi-tons survivent.
 * ------------------------------------------------------------------------- */

#define SCALE_R 1.0f
#define SCALE_G 1.2f
#define SCALE_B 0.8f

const unsigned char vg5k_palette[8 * 4] = {
    0xFF, 0xFF, 0xFF, 0xFF, /* 0 blanc   */
    0x00, 0xFF, 0xFF, 0xFF, /* 1 cyan    */
    0xFF, 0x00, 0xFF, 0xFF, /* 2 magenta */
    0x00, 0x00, 0xFF, 0xFF, /* 3 bleu    */
    0xFF, 0xFF, 0x00, 0xFF, /* 4 jaune   */
    0x00, 0xFF, 0x00, 0xFF, /* 5 vert    */
    0xFF, 0x00, 0x00, 0xFF, /* 6 rouge   */
    0x00, 0x00, 0x00, 0xFF  /* 7 noir    */
};

static const float C_PAL[8][3] = {
    {SCALE_R, SCALE_G, SCALE_B}, /* 0 blanc   */
    {0.0f, SCALE_G, SCALE_B},    /* 1 cyan    */
    {SCALE_R, 0.0f, SCALE_B},    /* 2 magenta */
    {0.0f, 0.0f, SCALE_B},       /* 3 bleu    */
    {SCALE_R, SCALE_G, 0.0f},    /* 4 jaune   */
    {0.0f, SCALE_G, 0.0f},       /* 5 vert    */
    {SCALE_R, 0.0f, 0.0f},       /* 6 rouge   */
    {0.0f, 0.0f, 0.0f}           /* 7 noir    */
};

/* ---------------------------------------------------------------------------
 * Matrice de tramage ordonne 4x2 en pixels logiques (meme choix que le
 * patch exoquant : seuils reguliers, centrosymetriques ; tuile visuelle
 * 4x4 carree sur pixels physiques 2:1). Indexee par (y & 3, x & 1).
 * AUCUNE diffusion d erreur : la perturbation ne depend que de la
 * position et du contenu local -> reutilisation des caracteres.
 * ------------------------------------------------------------------------- */

static const unsigned char DITHER_4X2[4][2] = {{28, 113}, {142, 227}, {85, 56}, {199, 170}};

/* ---------------------------------------------------------------------------
 * Les 70 sous-ensembles de 4 couleurs parmi 8 (bit c = couleur c).
 * Construits une fois par appel (ordre fixe -> determinisme).
 * ------------------------------------------------------------------------- */

#define N_SUBSETS 70

static unsigned int subsets4[N_SUBSETS];

static void build_subsets(void)
{
    int m, n = 0, k, t;

    for (m = 0; m < 256; m++)
    {
        k = 0;
        t = m;
        while (t)
        {
            k += t & 1;
            t >>= 1;
        }
        if (k == 4)
            subsets4[n++] = (unsigned int)m;
    }
    /* n == N_SUBSETS par construction : C(8,4) = 70 */
}

/* ---------------------------------------------------------------------------
 * Distance carree d un point t au segment [a,b] (projection bornee).
 * Mesure la capacite d un MELANGE des deux couleurs a representer la
 * cible : si le segment passe pres de t, une trame dosant les deux
 * couleurs peut la rendre ; sinon, aucune proportion ne le peut.
 * La projection bornee couvre aussi le cas des cibles proches d une
 * couleur seule (s = 0 ou 1 -> distance a la couleur).
 * ------------------------------------------------------------------------- */

static float dist2_seg(const float *t, const float *a, const float *b)
{
    float ab[3];
    float dd, s, de;
    int c;

    dd = 0.0f;
    for (c = 0; c < 3; c++)
    {
        ab[c] = b[c] - a[c];
        dd += ab[c] * ab[c];
    }
    if (dd <= 0.0f)
    { /* a == b : couleur isolee */
        de = 0.0f;
        for (c = 0; c < 3; c++)
        {
            float d = t[c] - a[c];
            de += d * d;
        }
        return de;
    }
    s = 0.0f;
    for (c = 0; c < 3; c++)
        s += (t[c] - a[c]) * ab[c];
    s /= dd;
    if (s < 0.0f)
        s = 0.0f;
    if (s > 1.0f)
        s = 1.0f;
    de = 0.0f;
    for (c = 0; c < 3; c++)
    {
        float d = a[c] + s * ab[c] - t[c];
        de += d * d;
    }
    return de;
}

/* ---------------------------------------------------------------------------
 * Meilleur sous-ensemble de 4 couleurs pour une cellule : minimise la
 * somme des distances (espace gamma pondere) des 40 cibles au plus
 * proche des 6 segments de la palette (mesure de ditherabilite :
 * pour un gris neutre, seul un sous-ensemble contenant blanc et noir
 * donne une distance quasi nulle -> jamais de teinte parasite).
 * Egalites : premier dans
 * l ordre d enumeration (departage deterministe).
 * ------------------------------------------------------------------------- */

static unsigned int best_palette(const float target[40][3])
{
    int s, p, j;
    int best_s = 0;
    float best_cost = 1e30f;

    for (s = 0; s < N_SUBSETS; s++)
    {
        unsigned int mask = subsets4[s];
        float cost = 0.0f;
        int c[4], nc = 0;

        for (j = 0; j < 8; j++)
            if (mask & (1u << j))
                c[nc++] = j;

        for (p = 0; p < 40; p++)
        {
            const float *t = target[p];
            float dmin = 1e30f;

            for (j = 0; j < nc; j++)
            {
                const float *v = C_PAL[c[j]];
                float dr = t[0] - v[0];
                float dg = t[1] - v[1];
                float db = t[2] - v[2];
                float d = dr * dr + dg * dg + db * db;
                if (d < dmin)
                    dmin = d;
            }
            cost += dmin;
        }
        if (cost < best_cost)
        {
            best_cost = cost;
            best_s = s;
        }
    }
    return subsets4[best_s];
}

/* ============================================================================
 * dither_to_vg5k_palette
 * ==========================================================================*/

unsigned char *dither_to_vg5k(unsigned char *image, int w, int h)
{
    int ncx, ncy, cy, cx, px, py, k, j;
    unsigned char *out;

    if (image == NULL)
        return NULL;
    if (w < VG5K_CELL_W || h < VG5K_CELL_H)
        return NULL;
    if ((w % VG5K_CELL_W) != 0 || (h % VG5K_CELL_H) != 0)
        return NULL;

    out = (unsigned char *)malloc((size_t)w * (size_t)h);
    if (out == NULL)
        return NULL;

    build_subsets();

    ncx = w / VG5K_CELL_W;
    ncy = h / VG5K_CELL_H;

    for (cy = 0; cy < ncy; cy++)
    {
        for (cx = 0; cx < ncx; cx++)
        {
            float target[40][3];
            unsigned int mask;
            int cols[4], nc = 0;
            int x0 = cx * VG5K_CELL_W;
            int y0 = cy * VG5K_CELL_H;

            /* 1) cibles : source en espace gamma pondere
             *                 (aucune erreur diffusee)                                   */
            k = 0;
            for (py = 0; py < VG5K_CELL_H; py++)
                for (px = 0; px < VG5K_CELL_W; px++, k++)
                {
                    int g = (y0 + py) * w + (x0 + px);
                    target[k][0] = image[4 * g + 0] / 255.0f * SCALE_R;
                    target[k][1] = image[4 * g + 1] / 255.0f * SCALE_G;
                    target[k][2] = image[4 * g + 2] / 255.0f * SCALE_B;
                }

            /* 2) meilleure palette 4 couleurs pour cette cellule */
            mask = best_palette(target);
            for (j = 0; j < 8; j++)
                if (mask & (1u << j))
                    cols[nc++] = j;

            /* 3) affectation avec perturbation ordonnee (facon
             *                 exoquant) : les deux couleurs les plus proches de
             *                 la cible definissent un axe ; la cible est decalee
             *                 le long de cet axe selon la matrice 4x2, puis la
             *                 couleur finale est la plus proche de la palette. */
            k = 0;
            for (py = 0; py < VG5K_CELL_H; py++)
                for (px = 0; px < VG5K_CELL_W; px++, k++)
                {
                    int gx = x0 + px, gy = y0 + py;
                    int g = gy * w + gx;
                    const float *t = target[k];
                    float d1 = 1e30f;
                    int c1 = cols[0], c2 = cols[1];
                    float v, t2[3], dmin = 1e30f;
                    int bi = 0;
                    int a, b;

                    /* meilleure paire : le segment de la palette qui
                     *                      passe le plus pres de la cible. Pour un gris
                     *                      neutre -> blanc/noir, jamais un axe chromatique
                     *                      (magenta et cie dans les fonds)             */
                    for (a = 0; a < nc; a++)
                        for (b = a + 1; b < nc; b++)
                        {
                            float d = dist2_seg(t, C_PAL[cols[a]], C_PAL[cols[b]]);
                            if (d < d1)
                            {
                                d1 = d;
                                c1 = cols[a];
                                c2 = cols[b];
                            }
                        }

                    /* decalage ordonne le long de l axe (c1 -> c2),
                     *                      facteur 0.8 comme exoquant, seuil de la matrice */
                    v = DITHER_4X2[gy & 3][gx & 1] / 255.0f - 0.5f;
                    t2[0] = t[0] + (C_PAL[c2][0] - C_PAL[c1][0]) * 0.8f * v;
                    t2[1] = t[1] + (C_PAL[c2][1] - C_PAL[c1][1]) * 0.8f * v;
                    t2[2] = t[2] + (C_PAL[c2][2] - C_PAL[c1][2]) * 0.8f * v;

                    /* couleur finale : plus proche de la palette */
                    for (j = 0; j < nc; j++)
                    {
                        const float *u = C_PAL[cols[j]];
                        float dr = t2[0] - u[0];
                        float dg = t2[1] - u[1];
                        float db = t2[2] - u[2];
                        float d = dr * dr + dg * dg + db * db;
                        if (d < dmin)
                        {
                            dmin = d;
                            bi = j;
                        }
                    }
                    out[g] = (unsigned char)cols[bi];
                }
        }
    }

    for (int i = 0, j = 0; i < w * h * 4; i += 4, j++)
    {
        unsigned char pal_idx = out[j];
        image[i] = vg5k_palette[pal_idx * 4];         // Rouge
        image[i + 1] = vg5k_palette[pal_idx * 4 + 1]; // Vert
        image[i + 2] = vg5k_palette[pal_idx * 4 + 2]; // Bleu
        image[i + 3] = vg5k_palette[pal_idx * 4 + 3]; // Alpha
    }

    return out;
}
