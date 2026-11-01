/* ============================================================================
 * vg5k_dither.h - Tramage quadrichrome pour VG5000 (lib cote PC)
 * ----------------------------------------------------------------------------
 * Transforme une image RGBA en image indexee sur les 8 couleurs VG5000,
 * en respectant la contrainte materielle quadrichrome DES LE TRAMAGE :
 * chaque cellule de 4x10 pixels n utilise au maximum que 4 couleurs.
 *
 * Usage :
 *   unsigned char *indexedPaletteData =
 *       dither_to_vg5k_palette(image, VG5K_WIDTH, VG5K_HEIGHT);
 *   ...
 *   free(indexedPaletteData);
 *
 * Entree  : image RGBA (4 octets par pixel, alpha ignore),
 *           largeur multiple de 4, hauteur multiple de 10
 *           (160x250 pour l ecran VG5000).
 * Sortie  : buffer de w*h octets, indices 0-7 de vg5k_palette :
 *           0 blanc, 1 cyan, 2 magenta, 3 bleu,
 *           4 jaune, 5 vert, 6 rouge, 7 noir.
 *           Renvoie NULL si image nulle, dimensions invalides ou
 *           erreur d allocation.
 *
 * Methode (deterministe, reproductible octet pour octet) :
 *   pour chaque cellule 4x10, de facon PUREMENT LOCALE (aucune
 *   erreur diffusee, aucune dependance au voisinage) :
 *   1. cible = source en espace couleur gamma pondere (meme espace
 *      qu exoquant : valeurs sRGB brutes x SCALE_R = 1.0,
 *      SCALE_G = 1.2, SCALE_B = 0.8, SANS linearisation) ;
 *   2. choix de la meilleure palette parmi les C(8,4) = 70
 *      sous-ensembles de 4 couleurs : minimisation exacte de la
 *      somme des distances des 40 cibles au plus proche des 6
 *      segments reliant les couleurs de la palette (mesure de
 *      ditherabilite : un melange dosant deux couleurs de la
 *      palette peut-il representer la cible ?) ;
 *   3. affectation avec perturbation ordonnee (facon exoquant) :
 *      la meilleure paire de couleurs de la palette (le segment
 *      passant le plus pres de la cible) definit un axe ; la cible
 *      est decalee le long de cet axe d un facteur
 *      issu de la matrice ordonnee 4x2 (celle du patch exoquant,
 *      tuile visuelle 4x4 carree sur pixels physiques 2:1), puis la
 *      couleur finale est la plus proche de la palette.
 *
 * Garantie : au plus 4 couleurs distinctes par cellule, donc
 *   - top_mask_reversed() trouve TOUTES les couleurs de la cellule,
 *   - r3_code() ne renvoie jamais 0xFF (plus de couleur absente),
 *   - l encodage en caracteres quadrichromes est sans bricolage.
 *
 * Reutilisation des caracteres : le traitement etant local et la
 * matrice de tramage periodique (2 en x, 4 en y), deux zones
 * sources identiques produisent des caracteres identiques, sauf
 * difference de phase verticale de la matrice (hauteur de cellule
 * 10, periode verticale 4 -> deux phases possibles).
 *
 * NB : cette lib definit le symbole global vg5k_palette ; si le
 * programme appelant definit deja ce symbole, supprimer l un des
 * deux (conflit a l edition de liens).
 * ==========================================================================*/

#ifndef VG5K_DITHER_H
#define VG5K_DITHER_H

#ifdef __cplusplus
extern "C" {
    #endif

    /* Palette VG5000 : 8 couleurs RGBA (octets R,G,B,A), ordre des indices :
     0 blanc, 1 cyan, 2 mage*nta, 3 bleu, 4 jaune, 5 vert, 6 rouge, 7 noir. */
    extern const unsigned char vg5k_palette[8 * 4];

    /* Dimensions d une cellule quadrichrome (pixels logiques) */
    #define VG5K_CELL_W 4
    #define VG5K_CELL_H 10

    /* Trame l image RGBA (w * h pixels, 4 octets chacun) en image indexee
     * 0-7 sur la palette VG5000, avec au plus 4 couleurs par cellule
     * de 4x10. Le buffer retourne (w * h octets) est a liberer par free().
     * Renvoie NULL sur erreur (dimensions invalides, allocation).      */
    unsigned char *dither_to_vg5k(unsigned char *image, int w, int h);

    #ifdef __cplusplus
}
#endif

#endif /* VG5K_DITHER_H */
