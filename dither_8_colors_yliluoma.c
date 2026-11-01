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

#include <stdio.h>
#include <stdlib.h>
#include "vg5k_dither_yliluoma2.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"



int main(int argc, char **argv)
{
    // 1. Vérification des arguments
    if (argc < 3)
    {
        fprintf(stderr, "Usage: %s <input.png> <output.png>\n", argv[0]);
        return 1;
    }

    // 2. Chargement de l'image source en RGBA (4 canaux)
    int w, h, channels;
    unsigned char *im = stbi_load(argv[1], &w, &h, &channels, 4);
    if (!im)
    {
        fprintf(stderr, "Erreur de lecture de l'image source '%s'.\n", argv[1]);
        return 1;
    }

    // 3. Appel du dither
    // srcim est modifié sur place en RGBA, et palette_indices reçoit le buffer 8-bits
    unsigned char *palette_indices = dither_y(im, w, h);
    if (!palette_indices)
    {
        fprintf(stderr, "Erreur d'allocation dans dither_y.\n");
        stbi_image_free(im);
        return 1;
    }

    // 4. Sauvegarde de l'image modifiée (srcim contient le résultat grâce au memcpy)
    if (!stbi_write_png(argv[2], w, h, 4, im, w * 4))
    {
        fprintf(stderr, "Erreur lors de l'écriture de l'image de sortie.\n");
    }
    else
    {
        printf("Rendu enregistré avec succès sous : %s\n", argv[2]);
    }

    // 5. Libération de la mémoire
    free(palette_indices); // Libère le buffer 'res' retourné
    stbi_image_free(im); // Libère le buffer initial

    return 0;
}

