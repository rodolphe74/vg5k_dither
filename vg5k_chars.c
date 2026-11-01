/* ============================================================================
 * vg5k_chars.c - implementation de vg5k_chars.h
 * ----------------------------------------------------------------------------
 * Compilation (cote PC, generateur d assets) :
 *   gcc -O2 -c vg5k_chars.c -o vg5k_chars.o
 * ==========================================================================*/

#include "vg5k_chars.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int current_bloc = 3;
int current_buffer = 0;
int current_char = 0;

void reset_vram_counter()
{
    current_bloc = 3;
    current_buffer = 0;
    current_char = 0;
}

int next_bloc()
{
    current_bloc++;
    if (current_bloc > 7)
        current_bloc = -1; // plus de bloc dispo
    return current_bloc;
}

int next_buffer()
{
    current_buffer++;
    if (current_buffer == 1)
    {
        current_buffer = 8;
    }
    else if (current_buffer == 32)
    {
        current_buffer = 0;
        if (next_bloc() == -1)
            return -1;
    }
    return current_buffer;
}

int next_car()
{
    current_char++;
    if (current_char == 4)
    {
        current_char = 0;
        if (next_buffer() == -1)
        {
            return -1;
        }
    }
    return current_char;
}

/* Renvoie le code 2 bits (0-3) de la couleur c dans la palette r3.
 *   0xFF si la couleur n est pas dans la palette. */
uint8_t r3_code(uint8_t r3, uint8_t c)
{
    // la palette r3 est inversee
    uint8_t bit = (uint8_t)(7 - (c & 7));
    uint8_t code = 0;
    uint8_t i;

    if (!(r3 & (uint8_t)(1 << bit)))
        return 0xFF; /* absente */
    for (i = 0; i < bit; i++)
        if (r3 & (uint8_t)(1 << i))
            code++;
    return code;
}

uint8_t top_mask_reversed(const uint8_t v[8])
{
    uint8_t mask = 0;
    uint8_t taken[8] = {0};
    int k, i;

    for (k = 0; k < 4; k++)
    { /* exactement 4 couleurs max */
        int best = -1;
        for (i = 0; i < 8; i++)
        {
            if (taken[i] || v[i] == 0)
                continue;
            if (best < 0 || v[i] > v[best])
                best = i;
        }
        if (best < 0)
            break;
        taken[best] = 1;
        mask |= (uint8_t)(1 << (7 - best));
    }
    return mask;
}

void img_make_buffer(const uint8_t *pix, uint8_t x_four, uint8_t y_ten, uint8_t w, uint8_t out40[40], uint8_t r34[4])
{
    memset(out40, 0, 40);

    // trouve une palette optimale
    // uint8_t r34[4] = {0};
    for (uint8_t k = 0; k < 4; k++)
    {
        uint8_t colors_count[8] = {0};
        uint8_t r3 = 0;
        for (uint8_t y = 0; y < 10; y++)
        {
            for (uint8_t x = 0; x < 4; x++)
            {
                uint8_t c = pix[(x_four + x + 4 * k) + (y_ten + y) * w];
                printf("%d ", c);
                colors_count[c]++;
            }
            printf("\n");
        }
        for (uint8_t i = 0; i < 8; i++)
        {
            printf("%d,", colors_count[i]);
        }
        r3 = top_mask_reversed(colors_count);
        r34[k] = r3;
        printf("p=%d\n", r3);
    }

    for (uint8_t y = 0; y < 10; y++)
    {
        for (uint8_t x = 0; x < 4 * 4; x++)
        {
            uint8_t k = x / 4;
            // uint8_t s = 7 - (x % 4) * 2 - 1;
            uint8_t s = (x % 4) * 2;
            uint8_t o = y * 4 + k;
            uint8_t c = pix[(x_four + x) + (y_ten + y) * w];
            uint8_t p = r3_code(r34[k], c);
            if (p == 0xFF)
            {
                // couleur absente de la palette
                p = 0; // TODO rechercher la couleur la plus proche
            }
            printf("%d[%d]|%d ", c, s, p);
            out40[o] |= (p << s);
        }
        printf("\n");
    }
}

void img_make_car(const uint8_t *pix, uint8_t x_four, uint8_t y_ten, uint8_t w, uint8_t h, uint8_t out10[10],
                  uint8_t r31[1])
{
    memset(out10, 0, 10);

    // trouve une palette optimale
    uint8_t colors_count[8] = {0};
    uint8_t r3 = 0;
    for (uint8_t y = 0; y < 10; y++)
    {
        for (uint8_t x = 0; x < 4; x++)
        {
            uint8_t c = pix[(x_four + x) + (y_ten + y) * w];
            colors_count[c]++;
        }
    }
    r3 = top_mask_reversed(colors_count);
    r31[0] = r3;

    for (uint8_t y = 0; y < 10; y++)
    {
        for (uint8_t x = 0; x < 4; x++)
        {
            uint8_t s = (x % 4) * 2;
            uint8_t o = y;

            uint8_t c = 0;
            if (x_four + x < w && y_ten + y < h)
            {
                c = pix[(x_four + x) + (y_ten + y) * w];
            }
            uint8_t p = r3_code(r31[0], c);
            if (p == 0xFF)
            {
                // couleur absente de la palette
                p = 0; // TODO rechercher la couleur la plus proche
            }
            out10[o] |= (p << s);
        }
    }
}

void melt_cars_into_buffer(uint8_t c0[10], uint8_t c1[10], uint8_t c2[10], uint8_t c3[10], uint8_t out40[40])
{
    for (int y = 0; y < 10; y++)
    {
        out40[y * 4] = c0[y];
        out40[y * 4 + 1] = c1[y];
        out40[y * 4 + 2] = c2[y];
        out40[y * 4 + 3] = c3[y];
    }
}

/* ------------------------------------------------------------------ */
/* Distance visuelle entre deux caracteres quadrichromes               */
/* ------------------------------------------------------------------ */

/* Couleur VG5000 (0-7) affichee par un code 2 bits dans la palette
 *   r3 : n-ieme bit a 1 de r3 (comptes du bit 0) -> couleur 7-bit.
 *   Renvoie 0xFF si le code ne correspond a aucune couleur (palettes
 *   a moins de 4 couleurs : codes au-dela du dernier bit a 1).      */
uint8_t code_color(uint8_t r3, uint8_t code)
{
    uint8_t bit, n = 0;
    for (bit = 0; bit < 8; bit++)
    {
        if (r3 & (uint8_t)(1 << bit))
        {
            if (n == code)
                return (uint8_t)(7 - bit);
            n++;
        }
    }
    return 0xFF;
}

/* Distance visuelle entre deux caracteres quadrichromes.
 *   a[10] avec sa palette r3a, b[10] avec sa palette r3b.
 *
 *   Pour chacun des 40 pixels on calcule la couleur REELLEMENT
 *   affichee (via la palette de son caractere) puis la distance
 *   entre les deux couleurs : nombre de composantes R,G,B differentes
 *   (R = ~bit0, G = ~bit1, B = ~bit2 de la couleur) soit le nombre de
 *   bits differents entre les deux numeros de couleur (0 a 3).
 *   Couleur indefinie (code sans couleur) -> distance 3.
 *
 *   Renvoie la somme sur les 40 pixels : 0 si les caracteres sont
 *   visuellement identiques (y compris si les codes 2 bits sont
 *   permutes dans la palette), maximum 120 (40 x 3).
 *   Exemples : rouge/noir -> 2, blanc/noir -> 3, rouge/vert -> 2,
 *   deux caracteres de forme identique mais couleurs proches ->
 *   petite distance ; forme decalee d un pixel -> grande distance. */
uint8_t char_dist(const uint8_t a[10], uint8_t r3a, const uint8_t b[10], uint8_t r3b)
{
    uint16_t d = 0;
    uint8_t t, p;

    for (t = 0; t < 10; t++)
    {
        uint8_t ba = a[t];
        uint8_t bb = b[t];
        for (p = 0; p < 4; p++)
        {
            uint8_t ca = code_color(r3a, (uint8_t)((ba >> (2 * p)) & 3));
            uint8_t cb = code_color(r3b, (uint8_t)((bb >> (2 * p)) & 3));
            if (ca != cb)
            {
                if (ca == 0xFF || cb == 0xFF)
                    d += 3;
                else
                {
                    uint8_t x = (uint8_t)(ca ^ cb);
                    d += (uint16_t)((x & 1) + ((x >> 1) & 1) + ((x >> 2) & 1));
                }
            }
        }
    }
    return (uint8_t)d;
}

/* Append n tampons sous forme d un tableau [n][40], format
 *   identique aux gfx_ball_* : 12 octets par ligne, groupes de 4. */
void gfx_append_data40n(FILE *f, const char *name, const uint8_t d[][40], int n)
{
    int k, i;
    fprintf(f, "static const uint8_t %s[%d][40] = {\n", name, n);
    for (k = 0; k < n; k++)
    {
        fprintf(f, "    {");
        for (i = 0; i < 40; i++)
        {
            fprintf(f, "0x%02X", d[k][i]);
            if (i < 39)
                fputc(',', f);
            if (i % 12 == 11 && i < 39)
                fprintf(f, "\n     ");
            else if (i % 4 == 3 && i < 39)
                fputc(' ', f);
        }
        fprintf(f, "}%s\n", (k < n - 1) ? "," : "");
    }
    fprintf(f, "};\n");
}

void debug_char(uint8_t ch[10], uint8_t attr)
{
    static const char *NAME[8] = {"blanc", "cyan", "magenta", "bleu", "jaune", "vert", "rouge", "noir"};
    uint8_t t, p, c, rank, bit;

    /* Attribut : chaque bit a 1 de attr = une couleur, rang = code */
    printf("attr $%02X :", attr);
    rank = 0;
    for (bit = 0; bit < 8; bit++)
    {
        if (attr & (1 << bit))
        {
            printf("  code %u = %s", rank, NAME[7 - bit]);
            rank++;
        }
    }
    for (c = rank; c < 4; c++)
        printf("  code %u = --", c); /* codes sans couleur */
    printf("\n\n");

    /* Matrice 4x10 : ligne = tranche t, colonnes = pixels de gauche a droite */
    for (t = 0; t < 10; t++)
    {
        printf("    ");
        for (p = 0; p < 4; p++)
            printf(" %u", (ch[t] >> (2 * p)) & 3);
        printf("\n");
    }
}

/* ============================================================================
 * vg5k_encode_char
 * ==========================================================================*/

int vg5k_encode_char(const unsigned char *cell40, unsigned char ch[10], unsigned char *attr)
{
    unsigned char count[8] = {0}; /* occurrences par couleur     */
    unsigned char taken[8] = {0}; /* couleurs conservees          */
    unsigned char keep[4];        /* couleurs conservees, par     */
    /* ordre de presence decroissant*/
    unsigned char remap[8]; /* couleur effective apres remap*/
    unsigned char mask = 0;
    int n_keep = 0;
    int i, b, k, c;

    if (cell40 == NULL || ch == NULL || attr == NULL)
        return -1;

    /* 1) histogramme des couleurs de la cellule */
    for (i = 0; i < 40; i++)
        count[cell40[i] & 7]++;

    /* 2) contrainte quadrichrome : on conserve au plus 4 couleurs, les
     *         plus presentes. A egalite, la plus petite couleur gagne
     *         (determinisme). Si la cellule utilise deja 4 couleurs ou
     *         moins, aucune n est ecartee : comportement inchange.       */
    while (n_keep < 4)
    {
        int best = -1;
        for (c = 0; c < 8; c++)
        {
            if (taken[c] || count[c] == 0)
                continue;
            if (best < 0 || count[c] > count[best])
                best = c;
        }
        if (best < 0)
            break; /* plus de couleur disponible */
        taken[best] = 1;
        keep[n_keep++] = (unsigned char)best;
    }

    /* 3) les couleurs ecartees (les moins presentes, au-dela de la
     *         4e) sont remplacees par la couleur conservee la plus proche :
     *         distance = nombre de composantes R, G, B differentes (xor
     *         sur les indices, comme vg5k_char_dist). A egalite, la
     *         premiere conservee par ordre de presence gagne.            */
    for (k = 0; k < 8; k++)
    {
        int best = keep[0], bestd = 4;
        if (taken[k])
        {
            remap[k] = (unsigned char)k;
            continue;
        }
        for (i = 0; i < n_keep; i++)
        {
            unsigned char x = (unsigned char)(k ^ keep[i]);
            int d = (x & 1) + ((x >> 1) & 1) + ((x >> 2) & 1);
            if (d < bestd)
            {
                bestd = d;
                best = keep[i];
            }
        }
        remap[k] = (unsigned char)best;
    }

    /* 4) attr : palette des couleurs conservees (bit (7-c) = couleur c) */
    for (i = 0; i < n_keep; i++)
        mask |= (unsigned char)(1u << (7 - keep[i]));
    *attr = mask;

    /* 5) codes 2 bits : rang du bit comptes du bit 0 */
    memset(ch, 0, 10);
    for (i = 0; i < 40; i++)
    {
        unsigned char col = remap[cell40[i] & 7];
        unsigned char code = 0;
        unsigned char bit = (unsigned char)(7 - col);
        for (b = 0; b < bit; b++)
            if (mask & (1u << b))
                code++;
        ch[i / 4] |= (unsigned char)(code << ((i % 4) * 2));
    }
    return 0;
}

unsigned char vg5k_char_dist(const unsigned char a[10], unsigned char attr_a, const unsigned char b[10],
                             unsigned char attr_b)
{
    unsigned short d = 0;
    unsigned char t, p;

    for (t = 0; t < 10; t++)
    {
        unsigned char ba = a[t];
        unsigned char bb = b[t];
        for (p = 0; p < 4; p++)
        {
            unsigned char ca = code_color(attr_a, (unsigned char)((ba >> (2 * p)) & 3));
            unsigned char cb = code_color(attr_b, (unsigned char)((bb >> (2 * p)) & 3));
            if (ca != cb)
            {
                if (ca == 0xFF || cb == 0xFF)
                {
                    d += 3;
                }
                else
                {
                    unsigned char x = (unsigned char)(ca ^ cb);
                    d += (unsigned short)((x & 1) + ((x >> 1) & 1) + ((x >> 2) & 1));
                }
            }
        }
    }
    return (unsigned char)d;
}

int vg5k_budget_chars(const unsigned char (*cells)[10], const unsigned char *attrs, int n_cells, int budget,
                      unsigned char (*out_chars)[10], unsigned char *out_attrs, int *out_remap)
{
    unsigned char (*uchars)[10] = NULL;
    unsigned char *uattr = NULL;
    unsigned char *dmat = NULL;
    int *umap = NULL;
    int *parent = NULL;
    int *ucount = NULL;
    int *finalidx = NULL;
    int n_unique = 0;
    int n_active, m;
    int i, j, u, v, w;

    if (cells == NULL || attrs == NULL || out_chars == NULL || out_attrs == NULL || out_remap == NULL)
        return -1;
    if (n_cells <= 0 || budget <= 0)
        return -1;

    uchars = (unsigned char (*)[10])malloc((size_t)n_cells * 10);
    uattr = (unsigned char *)malloc((size_t)n_cells);
    umap = (int *)malloc((size_t)n_cells * sizeof(int));
    if (uchars == NULL || uattr == NULL || umap == NULL)
        goto fail;

    /* 1) deduplication visuelle : distance 0 = meme affichage.
     *         raccourci memcmp pour le cas exact (frequent).           */
    for (i = 0; i < n_cells; i++)
    {
        for (j = 0; j < n_unique; j++)
        {
            if (uattr[j] == attrs[i] && memcmp(uchars[j], cells[i], 10) == 0)
                break;
            if (vg5k_char_dist(cells[i], attrs[i], uchars[j], uattr[j]) == 0)
                break;
        }
        umap[i] = j;
        if (j == n_unique)
        {
            memcpy(uchars[j], cells[i], 10);
            uattr[j] = attrs[i];
            n_unique++;
        }
    }

    /* 2) deja sous le budget : sortie directe */
    if (n_unique <= budget)
    {
        for (i = 0; i < n_unique; i++)
        {
            memcpy(out_chars[i], uchars[i], 10);
            out_attrs[i] = uattr[i];
        }
        for (i = 0; i < n_cells; i++)
            out_remap[i] = umap[i];
        free(uchars);
        free(uattr);
        free(umap);
        free(dmat);
        free(parent);
        free(ucount);
        free(finalidx);
        return n_unique;
    }

    /* 3) matrice des distances entre caracteres uniques */
    dmat = (unsigned char *)malloc((size_t)n_unique * (size_t)n_unique);
    parent = (int *)malloc((size_t)n_unique * sizeof(int));
    ucount = (int *)malloc((size_t)n_unique * sizeof(int));
    finalidx = (int *)malloc((size_t)n_unique * sizeof(int));
    if (dmat == NULL || parent == NULL || ucount == NULL || finalidx == NULL)
        goto fail;

    for (i = 0; i < n_unique; i++)
        for (j = 0; j < i; j++)
            dmat[i * n_unique + j] = dmat[j * n_unique + i] = vg5k_char_dist(uchars[i], uattr[i], uchars[j], uattr[j]);

    for (i = 0; i < n_unique; i++)
    {
        parent[i] = i;
        finalidx[i] = -1;
        ucount[i] = 0;
    }
    for (i = 0; i < n_cells; i++)
        ucount[umap[i]]++;

    /* 4) fusions gloutonnes : la paire active la plus proche,
     *         representant = le plus utilise, distance au pire cas      */
    n_active = n_unique;
    while (n_active > budget)
    {
        int bi = -1, bj = -1;
        unsigned char best = 0xFF;

        for (u = 0; u < n_unique; u++)
        {
            if (parent[u] != u)
                continue;
            for (v = 0; v < u; v++)
            {
                if (parent[v] != v)
                    continue;
                if (dmat[u * n_unique + v] < best)
                {
                    best = dmat[u * n_unique + v];
                    bi = u;
                    bj = v;
                }
            }
        }
        if (bi < 0)
            break; /* plus de paire active (defensif) */

        if (ucount[bi] < ucount[bj])
        {
            int t = bi;
            bi = bj;
            bj = t;
        }
        parent[bj] = bi;
        ucount[bi] += ucount[bj];
        n_active--;

        /* mise a jour : distance max (lien complet) */
        for (w = 0; w < n_unique; w++)
        {
            unsigned char du, dv, dm;
            if (parent[w] != w || w == bi)
                continue;
            du = dmat[bi * n_unique + w];
            dv = dmat[bj * n_unique + w];
            dm = (du > dv) ? du : dv;
            dmat[bi * n_unique + w] = dm;
            dmat[w * n_unique + bi] = dm;
        }
    }

    /* 5) compression des chaines + sortie compacte */
    for (i = 0; i < n_unique; i++)
    {
        int r = i;
        while (parent[r] != r)
            r = parent[r];
        parent[i] = r;
    }
    m = 0;
    for (i = 0; i < n_unique; i++)
    {
        int r = parent[i];
        if (finalidx[r] == -1)
        {
            memcpy(out_chars[m], uchars[r], 10);
            out_attrs[m] = uattr[r];
            finalidx[r] = m;
            m++;
        }
    }
    for (i = 0; i < n_cells; i++)
        out_remap[i] = finalidx[parent[umap[i]]];

    free(uchars);
    free(uattr);
    free(umap);
    free(dmat);
    free(parent);
    free(ucount);
    free(finalidx);
    return m;

fail:
    free(uchars);
    free(uattr);
    free(umap);
    free(dmat);
    free(parent);
    free(ucount);
    free(finalidx);
    return -1;
}
