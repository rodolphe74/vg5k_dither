// cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -B .\build -DCMAKE_EXPORT_COMPILE_COMMANDS=1
// ninja

#include "vg5k_chars.h"
#include "vg5k_dither_exoq.h"
// #include "vg5k_dither_tetrapal.h"
// #include "vg5k_dither_yliluoma2.h"
// #include <exoquant.h>
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

// static int current_bloc = 3;
// static int current_buffer = 0;
// static int current_char = 0;

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
    // unsigned char *indexedPaletteData = dither_y(image, VG5K_WIDTH, VG5K_HEIGHT);
    // unsigned char *indexedPaletteData = dither_t(image, VG5K_WIDTH, VG5K_HEIGHT);
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
    int max_index = 0;
    // int found = 0;
    int x = 0, y = 0;
    reset_vram_counter();

    // 1) encodage de toutes les cellules 4x10 en caracteres quadrichromes
    //    (ordre ligne par ligne : cellule y*40 + x/4)
    unsigned char cellchars[40 * 25][10];
    unsigned char cellattrs[40 * 25];
    unsigned char table_chars[TABLE_SIZE][10];
    unsigned char table_attrs[TABLE_SIZE];
    int remap[40 * 25];
    int i = 0;

    for (y = 0; y < VG5K_HEIGHT; y += 10)
    {
        for (x = 0; x < VG5K_WIDTH; x += 4)
        {
            /* vg5k_encode_char attend un tampon cell40 CONTIGU :
             * cell40[t * 4 + p] = pixel (p, t) de la cellule 4x10.
             * indexedPaletteData est un RASTER de stride VG5K_WIDTH :
             * lui passer &raster[y * 160 + x] fait lire 40 octets
             * consecutifs de la MEME ligne du raster, soit la ligne
             * du haut des 10 cellules situees a droite -> caracteres
             * en diagonale a l ecran. Extraire la cellule au bon
             * pas AVANT l appel.                                  */
            unsigned char cell40[40];
            int t, p;
            for (t = 0; t < 10; t++)
                for (p = 0; p < 4; p++)
                    cell40[t * 4 + p] = indexedPaletteData[(y + t) * VG5K_WIDTH + x + p];

            if (vg5k_encode_char(cell40, cellchars[i], &cellattrs[i]) != 0)
            {
                fprintf(stderr, "cellule (%d,%d) : plus de 4 couleurs\n", x, y);
                return 1;
            }
            i++;
        }
    }

    // 2) budget global : deduplication puis fusions des paires de
    //    caracteres les plus proches jusqu a respecter TABLE_SIZE.
    //    remap[c] = index du caractere final de la cellule c.
    max_index = vg5k_budget_chars(cellchars, cellattrs, 40 * 25, TABLE_SIZE, table_chars, table_attrs, remap);
    if (max_index < 0)
    {
        fprintf(stderr, "vg5k_budget_chars : erreur\n");
        return 1;
    }

    for (i = 0; i < max_index; i++)
    {
        memcpy(vram_table[i].data, table_chars[i], 10);
        vram_table[i].attr = table_attrs[i];
    }
    for (i = 0; i < 40 * 25; i++)
        char_list[char_index++] = remap[i];

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
     *      ils ne servent qu a completer le dernier tampon            */
    for (int i = max_index; i < total; i++)
    {
        memset(vram_table[i].data, 0, 10); /* car vide : tout code 0 */
        vram_table[i].attr = 0x80;         /* palette valide (blanc seul) */
    }

    fprintf(fredef, "const uint8_t quad[][1]={\n");
    for (int i = 0; i < loop_size; i++)
    {
        int j = i * 4;
        melt_cars_into_buffer(vram_table[j].data, vram_table[j + 1].data, vram_table[j + 2].data,
                              vram_table[j + 3].data, buffers[i]);
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
    fprintf(fredef, "    unpack_bloc(quad[i][0], &bloc, &tampon);\n");
    fprintf(fredef, "    spr_redef(bloc, tampon, buffers[i]);\n");

    fprintf(fredef, "}\n");

    // Affichage des caractères quadrichromes à partir de la liste pointant
    // sur la table

    fprintf(fredef, "const uint8_t vram[][3]={\n");
    for (int i = 0; i < char_index; i++)
    {
        uint8_t bloc_id = vram_table[char_list[i]].bloc_id;
        uint8_t buffer_id = vram_table[char_list[i]].buffer_id;
        uint8_t char_id = vram_table[char_list[i]].char_id;
        uint8_t attr = vram_table[char_list[i]].attr;
        fprintf(fredef, "{ %d, %d, %d }%s\n", pack_bloc(bloc_id, buffer_id), char_id, attr,
                (i < char_index - 1 ? "," : ""));
    }
    fprintf(fredef, "};\n");
    fprintf(fredef, "uint8_t col = 0, row = 0;\n");
    fprintf(fredef, "for (uint16_t i = 0; i < %d; i++) {\n", char_index);
    fprintf(fredef, "    col = i %% 40; row = i / 40;\n");
    fprintf(fredef, "    unpack_bloc(vram[i][0], &bloc, &tampon);\n");
    fprintf(fredef, "    vdp_put_quad_bloc(bloc, spr_code(tampon, vram[i][1]), vram[i][2], col, row);\n");
    fprintf(fredef, "}\n");
    fclose(fredef);

    FILE *fbuffers = fopen("buffers.h", "w");
    gfx_append_data40n(fbuffers, "buffers", buffers, loop_size);
    fclose(fbuffers);

    return 0;
}
