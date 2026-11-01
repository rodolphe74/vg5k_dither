#include "vg5k_dither_yliluoma2.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define USE_BAYER8x8 0
#define USE_ACCELERATED_SEARCH 0
#define USE_LUMA 0

#define N_COLOR 8
#define max(a, b) ((a) > (b) ? (a) : (b))

#if USE_BAYER8x8
static const unsigned char map[8 * 8] = {0,  48, 12, 60, 3,  51, 15, 63, 32, 16, 44, 28, 35, 19, 47, 31,
                                         8,  56, 4,  52, 11, 59, 7,  55, 40, 24, 36, 20, 43, 27, 39, 23,
                                         2,  50, 14, 62, 1,  49, 13, 61, 34, 18, 46, 30, 33, 17, 45, 29,
                                         10, 58, 6,  54, 9,  57, 5,  53, 42, 26, 38, 22, 41, 25, 37, 21};
#else
static const int map[4][2] = {{0, 3}, {4, 7}, {2, 1}, {6, 5}};
#endif

static const unsigned pal[8] = {
    0xFFFFFF, /* 0 blanc   */
    0x00FFFF, /* 1 cyan    */
    0xFF00FF, /* 2 magenta */
    0x0000FF, /* 3 bleu    */
    0xFFFF00, /* 4 jaune   */
    0x00FF00, /* 5 vert    */
    0xFF0000, /* 6 rouge   */
    0x000000  /* 7 noir    */
};

static unsigned luma[N_COLOR];

typedef struct
{
    int r;
    int g;
    int b;
} CouleurRGB;

#if USE_LUMA
double ColorCompare(const CouleurRGB *c1, const CouleurRGB *c2)
{
    double luma1 = (c1->r * 299 + c1->g * 587 + c1->b * 114) / (255.0 * 1000);
    double luma2 = (c2->r * 299 + c2->g * 587 + c2->b * 114) / (255.0 * 1000);
    double lumadiff = luma1 - luma2;

    double diffR = (c1->r - c2->r) / 255.0;
    double diffG = (c1->g - c2->g) / 255.0;
    double diffB = (c1->b - c2->b) / 255.0;

    return (diffR * diffR * 0.299 + diffG * diffG * 0.587 + diffB * diffB * 0.114) * 0.75 + lumadiff * lumadiff;
}
#else
double ColorCompare(const CouleurRGB *c1, const CouleurRGB *c2)
{
    double diffR = (c1->r - c2->r) / 255.0;
    double diffG = (c1->g - c2->g) / 255.0;
    double diffB = (c1->b - c2->b) / 255.0;

    return diffR * diffR + diffG * diffG + diffB * diffB;
}
#endif

int QSortColorCompare(const void *a, const void *b)
{
    double score = ColorCompare((const CouleurRGB *)a, (const CouleurRGB *)b);
    return (score > 0.0) - (score < 0.0);
}

int PaletteCompareLuma(const void *a, const void *b)
{
    unsigned index1 = *(const unsigned *)a;
    unsigned index2 = *(const unsigned *)b;
    return (luma[index1] > luma[index2]) - (luma[index1] < luma[index2]);
}

typedef struct MixingPlan
{
    unsigned colors[N_COLOR];
} MixingPlan;

unsigned stb_image_get_pixel(const uint8_t *data, int w, int h, int x, int y)
{
    if (x < 0 || x >= w || y < 0 || y >= h)
        return 0;

    size_t idx = (size_t)(y * w + x) * 4;

    uint8_t r = data[idx + 0];
    uint8_t g = data[idx + 1];
    uint8_t b = data[idx + 2];
    uint8_t a = data[idx + 3]; // Extraction du composant Alpha

    // Retourne le format 32 bits complet : 0xAARRGGBB
    return ((unsigned)a << 24) | ((unsigned)r << 16) | ((unsigned)g << 8) | b;
}

void stb_image_set_pixel(uint8_t *data, int w, int h, int x, int y, unsigned color_idx)
{
    if (x < 0 || x >= w || y < 0 || y >= h)
        return;

    size_t idx = (size_t)(y * w + x) * 4;

    uint8_t r = pal[color_idx] >> 16;
    uint8_t g = (pal[color_idx] >> 8) & 0xFF;
    uint8_t b = pal[color_idx] & 0xFF;

    data[idx + 0] = r;
    data[idx + 1] = g;
    data[idx + 2] = b;
    data[idx + 3] = 0xFF; // Ajout du composant Alpha (Opaque)
}

MixingPlan DeviseBestMixingPlan(unsigned color)
{
    MixingPlan result = {};
    /* CORRECTION 1 : Protection du canal Rouge contre le canal Alpha */
    const unsigned src[3] = {(color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF};
    unsigned proportion_total = 0;
    unsigned so_far[3] = {0, 0, 0};

    while (proportion_total < N_COLOR)
    {
        unsigned chosen_amount = 1;
        unsigned chosen = 0;
        const unsigned max_test_count = max(1u, proportion_total);

        double least_penalty = -1;
        for (unsigned index = 0; index < N_COLOR; ++index)
        {
            const unsigned color_val = pal[index];
            unsigned sum[3] = {so_far[0], so_far[1], so_far[2]};
            unsigned add[3] = {color_val >> 16, (color_val >> 8) & 0xFF, color_val & 0xFF};

#if USE_ACCELERATED_SEARCH
            for (unsigned p = 1; p <= max_test_count; p *= 2)
            {
                for (unsigned c = 0; c < 3; ++c)
                    sum[c] += add[c];
                for (unsigned c = 0; c < 3; ++c)
                    add[c] += add[c];
#else
            for (unsigned p = 1; p <= max_test_count; p++)
            {
                for (unsigned c = 0; c < 3; ++c)
                    sum[c] += add[c];
#endif
                unsigned t_count = proportion_total + p;
                unsigned test[3] = {sum[0] / t_count, sum[1] / t_count, sum[2] / t_count};

                CouleurRGB rgbtest = {test[0], test[1], test[2]};
                CouleurRGB rgbsrc = {src[0], src[1], src[2]};

                double penalty = ColorCompare(&rgbsrc, &rgbtest);

                if (penalty < least_penalty || least_penalty < 0)
                {
                    least_penalty = penalty;
                    chosen = index;
                    chosen_amount = p;
                }
            }
        }
        for (unsigned p = 0; p < chosen_amount; ++p)
        {
            if (proportion_total >= N_COLOR)
                break;
            result.colors[proportion_total++] = chosen;
        }

        const unsigned color_val = pal[chosen];
        unsigned palcolor[3] = {color_val >> 16, (color_val >> 8) & 0xFF, color_val & 0xFF};

        for (unsigned c = 0; c < 3; ++c)
            so_far[c] += palcolor[c] * chosen_amount;
    }
    qsort(result.colors, N_COLOR, sizeof(unsigned), PaletteCompareLuma);
    return result;
}

unsigned char *dither_y(unsigned char *src, int w, int h)
{
    unsigned char *out = (unsigned char *)malloc(w * h * 4);
    unsigned char *res = (unsigned char *)malloc((size_t)w * h);
    if (!out)
        return src;

    for (unsigned c = 0; c < N_COLOR; ++c)
    {
        unsigned r = pal[c] >> 16;
        unsigned g = (pal[c] >> 8) & 0xFF;
        unsigned b = pal[c] & 0xFF;
        luma[c] = r * 299 + g * 587 + b * 114;
    }

    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            unsigned color = stb_image_get_pixel(src, w, h, x, y);
#if USE_BAYER8x8
            unsigned map_value = map[(x & 7) + ((y & 7) << 3)];
            MixingPlan plan = DeviseBestMixingPlan(color);
            map_value = map_value * N_COLOR / 64;
#else
            // map correctement la valeur sur la taille du tableau de couleurs choisi (N_COLOR = 8).
            unsigned map_value = map[y & 3][x & 1];
            MixingPlan plan = DeviseBestMixingPlan(color);
            map_value = map_value * N_COLOR / 8;
#endif
            stb_image_set_pixel(out, w, h, x, y, plan.colors[map_value]);
            size_t idx_out = (size_t)y * w + x;
            res[idx_out] = (unsigned char)plan.colors[map_value];
        }
    }
    memcpy(src, out, w * h * 4);
    free(out);
    return res;
}
