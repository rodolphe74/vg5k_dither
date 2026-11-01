#include "vg5k_dither_tetrapal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tetrapal.h>

#define N_CANDIDATES 4
#define PALETTE_SIZE 8

// static const int dmap[4][2] = {{0, 3}, {4, 7}, {2, 1}, {6, 5}};
static const int dmap[4][2] = {{0, 6}, {4, 2}, {1, 7}, {5, 3}};
#define DMAP_H 4
#define DMAP_W 2
#define D_RANGE 8


static const float pal[PALETTE_SIZE * 3] = {
    1.0, 1.0, 1.0, /* 0 blanc    */
    0.0, 1.0, 1.0, /* 1 cyan     */
    1.0, 0,   1.0, /* 2 magenta  */
    0.0, 0,   1.0, /* 3 bleu     */
    1.0, 1.0, 0,   /* 4 jaune    */
    0,   1.0, 0,   /* 5 vert     */
    1.0, 0,   0,   /* 6 rouge    */
    0,   0,   0    /* 7 noir     */
};

typedef struct {
    int candidate;
    float weight;
} TetraNode;

static const float *current_palette = NULL;

static int compare_nodes(const void *a, const void *b)
{
    const TetraNode *node_a = (const TetraNode *)a;
    const TetraNode *node_b = (const TetraNode *)b;

    const float *color_a = &current_palette[node_a->candidate * 3];
    const float lum_a = 0.2126f * color_a[0] + 0.7152f * color_a[1] + 0.0722f * color_a[2];

    const float *color_b = &current_palette[node_b->candidate * 3];
    const float lum_b = 0.2126f * color_b[0] + 0.7152f * color_b[1] + 0.0722f * color_b[2];

    return (lum_a > lum_b) - (lum_a < lum_b);
}

void sort_by_luminance(int *candidates, float *weights, const float *palette, int n)
{
    if (n <= 1) return;

    TetraNode nodes[4];
    for (int i = 0; i < n; i++) {
        nodes[i].candidate = candidates[i];
        nodes[i].weight = weights[i];
    }

    current_palette = palette;
    qsort(nodes, (size_t)n, sizeof(TetraNode), compare_nodes);

    // Réinjection des valeurs triées dans les tableaux d'origine
    for (int i = 0; i < n; i++) {
        candidates[i] = nodes[i].candidate;
        weights[i] = nodes[i].weight;
    }
}


unsigned char *dither_t(unsigned char *image, int w, int h)
{
    unsigned char *out;
    int x, y;

    if (image == NULL)
        return NULL;

    out = (unsigned char *)malloc((size_t)w * (size_t)h);
    if (out == NULL)
        return NULL;


    float tetra_weights[N_CANDIDATES];
    int tetra_candidates[N_CANDIDATES];
    memset(tetra_weights, 0, sizeof(tetra_weights));
    memset(tetra_candidates, 0, sizeof(tetra_candidates));
    Tetrapal *tetrapal = tetrapal_new(pal, PALETTE_SIZE);

    for (y = 0; y < h; y++)
    {
        for (x = 0; x < w; x++)
        {
            unsigned char *px = &image[(y * w + x) * 4];
            int col[3];
            double threshold = (dmap[y % DMAP_H][x % DMAP_W]) / (double)D_RANGE;
            col[0] = px[0];
            col[1] = px[1];
            col[2] = px[2];
            float pixel[3] = {col[0] / 255.0, col[1] / 255.0, col[2] / 255.0};
            tetrapal_interpolate(tetrapal, pixel, tetra_candidates, tetra_weights);
            sort_by_luminance(tetra_candidates, tetra_weights, pal, N_CANDIDATES);
            double sum = 0.0;
            int selected = 0;
            for (int i = 0; i < N_CANDIDATES; i++)
            {
                sum += tetra_weights[i];
                if (threshold < sum)
                {
                    const float *color = &pal[tetra_candidates[i] * 3];
                    px[0] = (int)(color[0] * 255.0f + 0.5f);
                    px[1] = (int)(color[1] * 255.0f + 0.5f);
                    px[2] = (int)(color[2] * 255.0f + 0.5f);
                    out[y * w + x] = tetra_candidates[i];
                    selected = 1;
                    break;
                }
            }
            // Fallback si le seuil dépasse la somme totale
            if (!selected && N_CANDIDATES > 0) {
                const float *color = &pal[tetra_candidates[N_CANDIDATES - 1] * 3];
                px[0] = (int)(color[0] * 255.0f + 0.5f);
                px[1] = (int)(color[1] * 255.0f + 0.5f);
                px[2] = (int)(color[2] * 255.0f + 0.5f);
                out[y * w + x] = tetra_candidates[N_CANDIDATES - 1];
            }
        }
    }

    tetrapal_free(tetrapal);

    return out;
}
