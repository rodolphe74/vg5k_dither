// cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -B .\build -DCMAKE_EXPORT_COMPILE_COMMANDS=1
// ninja

#include "vg5k_dither_exoq.h"
#include "vg5k_chars.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_STATIC
#include <stb_image_resize2.h>

#define VG5K_WIDTH 160
#define VG5K_HEIGHT 250


// unsigned char vg5k_palette[8 * 4] = {
//     0xFF, 0xFF, 0xFF, 0xFF,
//     0x00, 0xFF, 0xFF, 0xFF,
//     0xFF, 0x00, 0xFF, 0xFF,
//     0x00, 0x00, 0xFF, 0xFF,
//     0xFF, 0xFF, 0x00, 0xFF,
//     0x00, 0xFF, 0x00, 0xFF,
//     0xFF, 0x00, 0x00, 0xFF,
//     0x00, 0x00, 0x00, 0xFF,
// };

/* PC, dans le generateur (vg5kdither.c) */
uint8_t pack_bloc(uint8_t bloc, uint8_t tampon)
{
    return (uint8_t)((bloc << 5) | (tampon & 0x1F));
}

/* Z80, dans disp.c */
void unpack_bloc(uint8_t v, uint8_t *bloc, uint8_t *tampon)
{
    *bloc = (uint8_t)(v >> 5);
    *tampon = (uint8_t)(v & 0x1F);
}

/**
 * Place une image brute dans un canvas plus grand avec gestion de l'opacité.
 *
 * @param src_data        Pixels de l'image d'origine.
 * @param src_w           Largeur de l'image d'origine.
 * @param src_h           Hauteur de l'image d'origine.
 * @param channels        Nombre de canaux (3 pour RGB, 4 pour RGBA).
 * @param canvas_w        Largeur du nouveau canvas.
 * @param canvas_h        Hauteur du nouveau canvas.
 * @param pos_x           Position X dans le canvas.
 * @param pos_y           Position Y dans le canvas.
 * @param is_white_bg     0 pour un fond NOIR opaque, 1 pour un fond BLANC opaque.
 */
unsigned char *place_image_in_canvas(const unsigned char *src_data, int src_w, int src_h, int channels, int canvas_w,
                                     int canvas_h, int pos_x, int pos_y, int is_white_bg)
{
    if (!src_data || src_w <= 0 || src_h <= 0 || canvas_w <= 0 || canvas_h <= 0 || channels <= 0)
    {
        return NULL;
    }

    if ((pos_x + src_w) > canvas_w || (pos_y + src_h) > canvas_h || pos_x < 0 || pos_y < 0)
    {
        return NULL;
    }

    size_t canvas_size = (size_t)canvas_w * canvas_h * channels;
    unsigned char *canvas_data = (unsigned char *)malloc(canvas_size);
    if (!canvas_data)
    {
        return NULL;
    }

    // --- REMPLISSAGE DU FOND INTELLIGENT ---
    if (channels == 4)
    {
        // Si RGBA, on remplit pixel par pixel pour forcer l'Alpha à 255 (Opaque)
        unsigned char r = is_white_bg ? 255 : 0;
        unsigned char g = is_white_bg ? 255 : 0;
        unsigned char b = is_white_bg ? 255 : 0;
        unsigned char a = 255; // 255 = Opaque (Anti-transparence)

        for (size_t i = 0; i < (size_t)canvas_w * canvas_h; i++)
        {
            canvas_data[i * 4 + 0] = r;
            canvas_data[i * 4 + 1] = g;
            canvas_data[i * 4 + 2] = b;
            canvas_data[i * 4 + 3] = a;
        }
    }
    else
    {
        // Si RGB (3 canaux) ou niveaux de gris (1 canal), un simple memset rapide suffit
        unsigned char color = is_white_bg ? 255 : 0;
        memset(canvas_data, color, canvas_size);
    }
    // ----------------------------------------

    // Copie ligne par ligne de l'image source
    for (int y = 0; y < src_h; y++)
    {
        const unsigned char *src_row = &src_data[y * src_w * channels];
        int target_y = y + pos_y;
        unsigned char *dst_row = &canvas_data[(target_y * canvas_w + pos_x) * channels];
        memcpy(dst_row, src_row, src_w * channels);
    }

    return canvas_data;
}

/**
 * Redimensionne (reduit) l'image si elle depasse 160x250 en gardant le ratio,
 * en utilisant la bibliotheque stb_image_resize2.
 *
 * @param inputImage   Pointeur vers l'image source (RGBA : 4 octets par pixel)
 * @param ix           Largeur originale de l'image
 * @param iy           Hauteur originale de l'image
 * @param resizedImage Pointeur vers le buffer de destination (alloue si NULL)
 * @param ox           Pointeur pour recuperer la nouvelle largeur calculee
 * @param oy           Pointeur pour recuperer la nouvelle hauteur calculee
 * @return             Pointeur vers l'image redimensionnee (resizedImage ou malloc)
 */
unsigned char *shrink_if_necessary(const unsigned char *inputImage, const int ix, const int iy,
                                   unsigned char *resizedImage, int *ox, int *oy)
{
    int do_resize = 0;
    float ratio_x = 1.0;
    float ratio_y = 1.0;
    float new_width = ix;
    float new_height = iy;

    if (ix >= 160)
    {
        ratio_x = ix / 320.0;
        do_resize = 1;
    }

    if (iy >= 250)
    {
        ratio_y = iy / 250.0;
        do_resize = 1;
    }

    float ratio = 1.0;
    if (do_resize)
    {
        ratio = (ratio_x > ratio_y ? ratio_x : ratio_y);
        new_width = ix / (ratio * 2.0);
        new_height = iy / ratio;
    }

    *ox = (int)roundf(new_width);
    *oy = (int)roundf(new_height);

    if (resizedImage == NULL)
    {
        resizedImage = (unsigned char *)malloc(*ox * *oy * 4);
        if (resizedImage == NULL)
        {
            return NULL; // Erreur d'allocation
        }
    }

    stbir_resize_uint8_linear(inputImage, ix, iy, 0, resizedImage, *ox, *oy, 0, STBIR_RGBA);

    return resizedImage;
}


/* Convertit une image indexee (indices dans pal, format 8*4 RGBA
 *   exoquant) en image RGBA 4 composantes pour stbi_write_png/bmp. */
void indexed_to_rgba(const uint8_t *in, const uint8_t *pal, uint8_t *rgba, int w, int h)
{
    int i;
    for (i = 0; i < w * h; i++)
    {
        rgba[4 * i + 0] = pal[in[i] * 4 + 0];
        rgba[4 * i + 1] = pal[in[i] * 4 + 1];
        rgba[4 * i + 2] = pal[in[i] * 4 + 2];
        rgba[4 * i + 3] = 255; /* pas de transparence VG5000 */
    }
}

int find_best_index(const uint8_t ch[10], uint8_t r, const struct VRAM_VG5K_QUAD_CHAR *vram_table, size_t table_size,
                    uint8_t max_distance)
{
    uint8_t min_distance = 255;
    int best_index = -1;
    size_t i;

    for (i = 0; i < table_size; i++)
    {
        uint8_t d = vg5k_char_dist(ch, r, vram_table[i].data, vram_table[i].attr);
        if (d < min_distance)
        { /* strict : premier gagnant en cas d egalite */
            min_distance = d;
            best_index = (int)i;
            if (min_distance == 0)
                break; /* on ne fera pas mieux */
        }
    }

    if (best_index < 0 || min_distance > max_distance)
        return -1;

    return best_index;
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        printf("argc:%d\n", argc);
        printf("arguments must be exactly [filename]\n");
        return 0;
    }

    char *fname = argv[1]; // filename

    int w, h, c;

    unsigned char *data = stbi_load(fname, &w, &h, &c, 4);

    printf("Image loaded:%d*%d*%d\n", w, h, c);

    int w_vg5k, h_vg5k;
    unsigned char *dataResized = NULL;
    dataResized = shrink_if_necessary(data, w, h, dataResized, &w_vg5k, &h_vg5k);

    unsigned char *image = place_image_in_canvas(dataResized, w_vg5k, h_vg5k, 4, VG5K_WIDTH, VG5K_HEIGHT, 0, 0, 0);
    printf("Image size:%d*%d*%d\n", w_vg5k, h_vg5k, c);

    // unsigned char *indexedPaletteData = dither_to_vg5k_palette(image, VG5K_WIDTH, VG5K_HEIGHT);
    unsigned char *indexedPaletteData = dither_to_vg5k(image, VG5K_WIDTH, VG5K_HEIGHT);
    printf("dithered!\n");

    stbi_write_png("dither.png", VG5K_WIDTH, VG5K_HEIGHT, 4, image, 4 * VG5K_WIDTH);

    free(dataResized);
    free(image);
    free(data);

// parcours de l'image et création d'une table de caracteres
#define TABLE_SIZE 500
    struct VRAM_VG5K_QUAD_CHAR vram_table[TABLE_SIZE];
    int char_list[40 * 25];
    int char_index = 0;
    uint8_t ch[10];
    uint8_t r[1];
    int max_index = 0;
    int table_index = 0;
    // int found = 0;
    int x, y;

    reset_vram_counter();

    for (y = 0; y < VG5K_HEIGHT; y += 10)
    {
        for (x = 0; x < VG5K_WIDTH; x += 4)
        {
            img_make_car(indexedPaletteData, x, y, VG5K_WIDTH, VG5K_HEIGHT, ch, r);

            int index = find_best_index(ch, r[0], vram_table, max_index, 0);
            if (index > -1)
            {
                table_index = index; /* existe deja -> reutilise */
            }
            else if (max_index < TABLE_SIZE)
            {
                table_index = max_index; /* nouvelle entree */
                memcpy(vram_table[max_index].data, ch, 10);
                vram_table[max_index].attr = r[0];
                max_index++;
            }
            else
            {
                /* table pleine, pas d exact : le plus proche sous un seuil */
                index = find_best_index(ch, r[0], vram_table, max_index, 20);
                table_index = (index > -1) ? index : 0; /* sinon car neutre */
            }
            char_list[char_index++] = table_index;
        }
    }

    printf("table size:%d\n", max_index);
    printf("char list size:%d\n", char_index);
    printf("x=%d y=%d\n", x, y);

    // création de la liste des blocs/tampons/caracteres
    // et des header c à inclure dans la compilation zcc
    reset_vram_counter();
    FILE *fredef = fopen("redef.h", "w");

    // creation de la liste des blocs/tampons/caracteres
    int total = (max_index + 3) & ~3; /* multiple de 4 superieur */
    uint8_t buffers[total / 4][40];
    uint8_t loop_size = total / 4;

    /* caracteres de bourrage : jamais references par char_list,
     *       ils ne servent qu a completer le dernier tampon            */
    for (int i = max_index; i < total; i++)
    {
        memset(vram_table[i].data, 0, 10); /* car vide : tout code 0 */
        vram_table[i].attr = 0x80;         /* palette valide (blanc seul) */
    }

    // fprintf(fredef, "const uint8_t quad[][2]={\n");
    fprintf(fredef, "const uint8_t quad[][1]={\n");
    for (int i = 0; i < loop_size; i++)
    {
        int j = i * 4;
        melt_cars_into_buffer(vram_table[j].data, vram_table[j + 1].data, vram_table[j + 2].data,
                              vram_table[j + 3].data, buffers[i]);
        // fprintf(fredef, "{ %d, %d }%s\n", current_bloc, current_buffer, (i < loop_size - 1 ? ",": ""));
        fprintf(fredef, "{ %d }%s\n", pack_bloc(current_bloc, current_buffer), (i < loop_size - 1 ? "," : ""));

        // char 0
        printf("j=%d bloc=%d buffer=%d char=%d\n", j, current_bloc, current_buffer, current_char);
        vram_table[j].bloc_id = current_bloc;
        vram_table[j].buffer_id = current_buffer;
        vram_table[j].char_id = current_char;

        // char 1
        next_car();
        printf("j=%d bloc=%d buffer=%d char=%d\n", j + 1, current_bloc, current_buffer, current_char);
        vram_table[j + 1].bloc_id = current_bloc;
        vram_table[j + 1].buffer_id = current_buffer;
        vram_table[j + 1].char_id = current_char;

        // char 2
        next_car();
        printf("j=%d bloc=%d buffer=%d char=%d\n", j + 2, current_bloc, current_buffer, current_char);
        vram_table[j + 2].bloc_id = current_bloc;
        vram_table[j + 2].buffer_id = current_buffer;
        vram_table[j + 2].char_id = current_char;

        // char 3
        next_car();
        printf("j=%d bloc=%d buffer=%d char=%d\n", j + 3, current_bloc, current_buffer, current_char);
        vram_table[j + 3].bloc_id = current_bloc;
        vram_table[j + 3].buffer_id = current_buffer;
        vram_table[j + 3].char_id = current_char;

        next_car(); // -> 0
    }
    fprintf(fredef, "};\n");
    fprintf(fredef, "uint8_t bloc, tampon;\n");
    fprintf(fredef, "for (uint8_t i = 0; i < %d; i++) {\n", loop_size);
    // fprintf(fredef, "    spr_redef(quad[i][0], quad[i][1], buffers[i]);\n");
    // fprintf(fredef, "    spr_redef(current_bloc, current_buffer, buffers[i]);\n");
    // fprintf(fredef, "    next_car();\n");

    fprintf(fredef, "    unpack_bloc(quad[i][0], &bloc, &tampon);\n");
    fprintf(fredef, "    spr_redef(bloc, tampon, buffers[i]);\n");

    fprintf(fredef, "}\n");

    // Affichage des caractères quadrichromes à partir de la liste pointant
    // sur la table

    // fprintf(fredef, "const uint8_t vram[][4]={\n");
    fprintf(fredef, "const uint8_t vram[][3]={\n");
    // int col = 0, row = 0;
    for (int i = 0; i < char_index; i++)
    {
        uint8_t bloc_id = vram_table[char_list[i]].bloc_id;
        uint8_t buffer_id = vram_table[char_list[i]].buffer_id;
        uint8_t char_id = vram_table[char_list[i]].char_id;
        uint8_t attr = vram_table[char_list[i]].attr;
        // printf("%d=%d %d %d\n", char_list[i], bloc_id, buffer_id, char_id);
        // fprintf(fredef, "{ %d, %d, %d, %d }%s\n", bloc_id, buffer_id, char_id, attr, (i < char_index - 1 ? ",": ""));
        fprintf(fredef, "{ %d, %d, %d }%s\n", pack_bloc(bloc_id, buffer_id), char_id, attr,
                (i < char_index - 1 ? "," : ""));
    }
    fprintf(fredef, "};\n");
    fprintf(fredef, "uint8_t col = 0, row = 0;\n");
    fprintf(fredef, "for (uint16_t i = 0; i < %d; i++) {\n", char_index);
    fprintf(fredef, "    col = i %% 40; row = i / 40;\n");
    // fprintf(fredef, "    vdp_put_quad_bloc(vram[i][0], spr_code(vram[i][1], vram[i][2]), vram[i][3], col, row);\n");
    fprintf(fredef, "    unpack_bloc(vram[i][0], &bloc, &tampon);\n");
    fprintf(fredef, "    vdp_put_quad_bloc(bloc, spr_code(tampon, vram[i][1]), vram[i][2], col, row);\n");
    // fprintf(fredef, "    vdp_put_quad_bloc(vram[i][0], spr_code(vram[i][1], vram[i][2]), vram[i][3], col, row);\n");
    fprintf(fredef, "}\n");
    fclose(fredef);

    FILE *fbuffers = fopen("buffers.h", "w");
    gfx_append_data40n(fbuffers, "buffers", buffers, loop_size);
    fclose(fbuffers);

    return 0;
}
