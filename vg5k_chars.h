/* ============================================================================
 * vg5k_chars.h - Encodage et budget de caracteres quadrichromes VG5000
 * ----------------------------------------------------------------------------
 * Complemente vg5k_dither.h : apres le tramage, cette deuxieme etape
 * transforme chaque cellule 4x10 en caractere quadrichrome (10 octets
 * + palette) puis garantit le budget global de caracteres uniques
 * (~500 : 5 blocs x 100 tampons de 4 caracteres sur l EF9345).
 *
 * Convention (identique a celle du generateur existant) :
 *   - attr (palette R3) : bit (7-c) = couleur c ;
 *   - code 2 bits d un pixel = rang du bit, compte du bit 0 ;
 *   - ch[y] (tranche y) : 4 pixels x 2 bits, bits 1-0 = pixel gauche.
 * ==========================================================================*/

#ifndef VG5K_CHARS_H
#define VG5K_CHARS_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>
#include <stdio.h>

    struct VRAM_VG5K_QUAD_CHAR
    {
        uint8_t data[10];
        uint8_t bloc_id;
        uint8_t buffer_id;
        uint8_t char_id;
        // uint8_t *buffer;
        uint8_t attr;
    };

    extern int current_bloc;
    extern int current_buffer;
    extern int current_char;

    void reset_vram_counter();

    int next_bloc();

    int next_buffer();

    int next_car();

    /* Renvoie le code 2 bits (0-3) de la couleur c dans la palette r3.
     *   0xFF si la couleur n est pas dans la palette. */
    uint8_t r3_code(uint8_t r3, uint8_t c);

    uint8_t top_mask_reversed(const uint8_t v[8]);

    void img_make_buffer(const uint8_t *pix, uint8_t x_four, uint8_t y_ten, uint8_t w, uint8_t out40[40],
                         uint8_t r34[4]);

    void img_make_car(const uint8_t *pix, uint8_t x_four, uint8_t y_ten, uint8_t w, uint8_t h, uint8_t out10[10],
                      uint8_t r31[1]);

    void melt_cars_into_buffer(uint8_t c0[10], uint8_t c1[10], uint8_t c2[10], uint8_t c3[10], uint8_t out40[40]);

    /* ------------------------------------------------------------------ */
    /* Distance visuelle entre deux caracteres quadrichromes               */
    /* ------------------------------------------------------------------ */

    /* Couleur VG5000 (0-7) affichee par un code 2 bits dans la palette
     *   r3 : n-ieme bit a 1 de r3 (comptes du bit 0) -> couleur 7-bit.
     *   Renvoie 0xFF si le code ne correspond a aucune couleur (palettes
     *   a moins de 4 couleurs : codes au-dela du dernier bit a 1).      */
    uint8_t code_color(uint8_t r3, uint8_t code);

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
    uint8_t char_dist(const uint8_t a[10], uint8_t r3a, const uint8_t b[10], uint8_t r3b);

    /* Append n tampons sous forme d un tableau [n][40], format
     *   identique aux gfx_ball_* : 12 octets par ligne, groupes de 4. */
    void gfx_append_data40n(FILE *f, const char *name, const uint8_t d[][40], int n);

    void debug_char(uint8_t ch[10], uint8_t attr);

    int vg5k_encode_char(const unsigned char *cell40, unsigned char ch[10], unsigned char *attr);

    /* Couleur affichee par un code dans la palette attr : n-ieme bit a 1
     *  (comptes du bit 0) -> couleur 7-bit. 0xFF si code sans couleur.  */
    unsigned char code_color(unsigned char attr, unsigned char code);

    /* Encode une cellule de 4x10 pixels en caractere quadrichrome.
     * cell40 : 40 indices 0-7 (palette VG5000), ordre ligne par ligne
     *          (cell40[y * 4 + x]).
     * ch     : 10 octets de sortie ; attr : palette de sortie.
     * Si la cellule utilise plus de 4 couleurs (contrainte quadrichrome
     * violee, ex. tramage sans garantie locale), la fonction n echoue
     * plus : elle conserve les 4 couleurs les plus presentes (a egalite,
     * la plus petite) et remplace chaque couleur ecartee par la couleur
     * conservee la plus proche (nombre de composantes RGB differentes).
     * Renvoie 0, ou -1 si un argument est nul.                     */
    int vg5k_encode_char(const unsigned char *cell40, unsigned char ch[10], unsigned char *attr);

    /* Distance visuelle entre deux caracteres quadrichromes :
     * 0 si identiques a l affichage (meme avec codes permutes dans la
     * palette), maximum 120 (40 pixels x 3 composantes differentes).
     * Couleur indefinie (code sans couleur) -> 3.                     */
    unsigned char vg5k_char_dist(const unsigned char a[10], unsigned char attr_a, const unsigned char b[10],
                                 unsigned char attr_b);

    /* Ramene le nombre de caracteres distincts sous le budget.
     * Entree  : cells/attrs = un caractere par cellule (n_cells entrees,
     *           sorties de vg5k_encode_char) ;
     *           budget = nombre maximal de caracteres a garder
     *           (500 pour tout l ecran, moins pour garder des blocs).
     * Sortie  : out_chars/out_attrs = table des caracteres retenus
     *           (dimensionner a budget) ;
     *           out_remap[i] = index (0..retour-1) du caractere final
     *           de la cellule i.
     * Methode : 1. deduplication visuelle (distance 0, gratuite) ;
     *           2. fusions repetees de la paire active la plus proche
     *              (agglomeration gloutonne, distances mises a jour au
     *              pire cas) jusqu a respecter le budget ; le
     *              representant conserve est le plus utilise.
     * L erreur visuelle se concentre ou elle coute le moins (paires les
     * plus proches d abord). Renvoie le nombre final de caracteres,
     * ou -1 sur erreur.                                              */
    int vg5k_budget_chars(const unsigned char (*cells)[10], const unsigned char *attrs, int n_cells, int budget,
                          unsigned char (*out_chars)[10], unsigned char *out_attrs, int *out_remap);

#ifdef __cplusplus
}
#endif

#endif /* VG5K_CHARS_H */
