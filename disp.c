/* ============================================================================
 *  sprites_vg5000.c - Gestionnaire de sprites pour VG5000u (Z80 + EF9345)
 *  ----------------------------------------------------------------------------
 *  Methode : celle du projet Vg5kGraphics (github.com/rodolphe74/Vg5kGraphics,
 *  eprouvee sur DCVG5K), qui redefinit des caracteres QUADRICHROMES :
 *
 *   - Ecriture directe des registres EF9345, SANS table $00AD :
 *     routine ROM $0286 (attendre "pret") puis $0D7C (ecrire :
 *     D = adresse du registre, E = valeur). Une ecriture a l'adresse
 *     "registre + 8" ecrit le registre ET execute la commande :
 *       $28 = R0+8 -> executer la commande passee dans E
 *       $2E = R6+8 -> ecrire R6 puis KRF (astuce "efface_ecran")
 *
 *   - Le mot "registre" designe l'adresse de selection ($20-$27 = R0-R7) :
 *       R1 = code/parametre, R2 = mode caractere, R3 = palette,
 *       R4/R5 = pointeur VRAM auxiliaire, R6 = ligne, R7 = colonne.
 *
 *   - Organisation ecran (init de Vg5kGraphics, octet pour octet) :
 *       ROR=$08, DOR=$03 (banques quadri Q3..Q7), TGS=$00,
 *       PAT=$77 (insertion video inhibee), MAT=$2E.
 *
 *   - Caractere quadrichrome : 4 px de large x 10 px de haut, un octet
 *     par tranche (4 pixels x 2 bits), 10 octets. Un caractere = 4 px
 *     LOGIQUES de large, affiches 2 px physiques chacun -> 160x250.
 *     La palette R3 est un masque 8 bits : bit (7-c) = couleur c
 *     (0 blanc, 1 cyan, 2 magenta, 3 bleu, 4 jaune, 5 vert, 6 rouge,
 *      7 noir). Les codes 2 bits des pixels numerotent les couleurs
 *     choisies, de R3 bit 0 vers bit 7 (cf. convertToZ8.py).
 *
 *   - VRAM EF9345 : 8 blocs de 1 Ko, decoupes en "tampons" de 40
 *     octets = 4 caracteres entrelaces (tranche n : cars 0,1,2,3).
 *     Les tampons utiles portent les numeros Y = 0 puis 8..31 ;
 *     les codes caracteres correspondants sont 0..3 puis 32..127.
 *     Redefinition : R4 = pointeur tampon (bloc 3 : "000" + n tampon),
 *     R5 = "11" + n d'octet, R1 = octet, commande $34 (OCT).
 *
 *   - Mode d'affichage R2 d'un caractere quadri de la banque Q3 :
 *     $D9 (= 11 quadri, 01 Q3, 1, 0, 01 incrustation).
 *
 *   - Lignes ecran : la ligne 0 s'ecrit en R6 = 0, les lignes 1..24
 *     en R6 = ligne + 7 (cf. display_sprite de Vg5kGraphics).
 *
 *  Compilation (z88dk, SCCZ80 recommande) :
 *      zcc +vg5k -O3 -create-app -o demo sprites_vg5000.c
 *      -> demo.k7 : CLOAD dans DCVG5K (dcvg5k.free.fr), sinon CALL 20480.
 *
 *  Sources : projet Vg5kGraphics (rodolphe74) et son convertToZ8.py ;
 *
 *  NB regles eprouvees pour SCCZ80 :
 *   1. Fichier en ASCII pur (sans accents).
 *   2. Blocs #asm/#endasm TOUJOURS dans le corps d une fonction C
 *      (jamais au niveau global) et SANS aucun commentaire dedans :
 *      apres un bloc #asm global, SCCZ80 perd le suivi des commentaires
 *      et parse leur texte comme du C (erreurs "Illegal Argument Name").
 * ==========================================================================*/

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Constantes materielles                                             */
/* ------------------------------------------------------------------ */

#define SCREEN_W        40          /* colonnes visibles                */
#define SCREEN_H        25          /* lignes visibles                  */
#define SYS_FLAG_ADDR   0x47FD      /* 0 = couper le rafraichissement   */

/* Adresses de selection des registres ($20-$27 = R0-R7) */
#define SEL_R0          0x20
#define SEL_R1          0x21
#define SEL_R2          0x22
#define SEL_R3          0x23
#define SEL_R4          0x24
#define SEL_R5          0x25
#define SEL_R6          0x26
#define SEL_R7          0x27
#define EXEC            0x28        /* R0+8 : executer la commande      */
#define EXEC_KRF_ROW    0x2E        /* R6+8 : ecrire R6 puis KRF        */

/* Commandes (passees a EXEC) */
#define CMD_KRF         0x00        /* ecrire le caractere courant      */
#define CMD_TGS         0x81
#define CMD_MAT         0x82
#define CMD_PAT         0x83
#define CMD_DOR         0x84
#define CMD_ROR         0x87
#define CMD_OCT         0x34        /* ecrire R1 en VRAM (R4/R5)        */

/* Organisation ecran de Vg5kGraphics (eprouvee sur DCVG5K) */
#define ROR_VG          0x08
#define DOR_VG          0x03        /* banques quadrichromes Q3..Q7     */
#define TGS_VG          0x00
#define PAT_VG          0x77        /* insertion video inhibee          */
#define MAT_VG          0x2E

/* Mode R2 d'affichage d'un caractere quadrichrome du bloc 3 */
#define R2_QUAD_Q3      0xD9

/* R2 selon le bloc (3 a 7), cf. R2_bits de convertToZ8.py :
 *  bloc 3 = $D9, 4 = $E1, 5 = $F1, 6 = $E9, 7 = $F9          */
static const uint8_t R2_QUAD_BLOC[8] = {
    0x00, 0x00, 0x00, 0xD9, 0xE1, 0xF1, 0xE9, 0xF9
};

/* Palettes R3 : bit (7-c) = couleur c
 *  (0 blanc, 1 cyan, 2 magenta, 3 bleu, 4 jaune, 5 vert, 6 rouge, 7 noir) */
#define ATTR_BLACK      0x01        /* { noir }                          */
// #define ATTR_BALL       0x03        /* { noir, rouge }                  */
#define ATTR_WHITE      0x80        /* { blanc }                        */

#define ATTR_BALL 0x11

/* ------------------------------------------------------------------ */
/* Interfacage avec les routines ROM (thunks assembleur)              */
/* ------------------------------------------------------------------ */

/* vdp_set : D = adresse de registre, E = valeur -> ROM $0286 (attendre
 *  pret) puis $0D7C (ecrire). Convention SCCZ80 : appelant nettoie.   */
void vdp_set(const uint8_t *de)
{
    #asm
    pop     bc
    pop     hl
    push    hl
    push    bc
    ld      d,(hl)
    inc     hl
    ld      e,(hl)
    push    ix
    push    iy
    call    $0286
    call    $0d7c
    pop     iy
    pop     ix
    ret
    #endasm
}

/* vdp_di : masque les interruptions (comme Vg5kGraphics). */
void vdp_di(void)
{
    #asm
    di
    ret
    #endasm
}

/* ------------------------------------------------------------------ */
/* Couche 1 : acces EF9345                                            */
/* ------------------------------------------------------------------ */

static uint8_t vdp_de[2];

void vdp_write(uint8_t reg, uint8_t val)
{
    vdp_de[0] = reg;
    vdp_de[1] = val;
    vdp_set(vdp_de);
}

static uint8_t vdp_row(uint8_t y)
{
    return (uint8_t)(y == 0 ? 0 : y + 7);
}

void spr_init(void)
{
    *(uint8_t *)SYS_FLAG_ADDR = 0;
    vdp_di();

    vdp_write(SEL_R1, ROR_VG); vdp_write(EXEC, CMD_ROR);
    vdp_write(SEL_R1, DOR_VG); vdp_write(EXEC, CMD_DOR);
    vdp_write(SEL_R1, TGS_VG); vdp_write(EXEC, CMD_TGS);
    vdp_write(SEL_R1, PAT_VG); vdp_write(EXEC, CMD_PAT);
    vdp_write(SEL_R1, MAT_VG); vdp_write(EXEC, CMD_MAT);
}

/* Redefinit un tampon = 4 caracteres quadrichromes de 4x10 px.
 *  Organisation des 40 octets (cf. convertToZ8.py) : 10 tranches
 *  horizontales de 4 octets, data40[4*t + c] = tranche t (0..9) du
 *  caractere c (0..3). Dans un octet : 4 pixels x 2 bits, bits 1-0
 *  = pixel de gauche, la valeur 2 bits = rang de la couleur dans la
 *  palette R3 (bit 7-c = couleur c).
 *
 *  bloc   : 3 a 7 (memoire auxiliaire de l EF9345)
 *  tampon : 0 a 31, mais 1 a 7 interdits (le piege) : apres le
 *           tampon 0 la numerotation reprend a 8, soit 0 puis 8..31
 *           par bloc, soit 100 caracteres redefinissables par bloc.  */
void spr_redef(uint8_t bloc, uint8_t tampon, const uint8_t *data40) /*__z88dk_callee*/
{
    uint8_t k;
    uint8_t r4;
    uint8_t r5;

    if (bloc < 3 || bloc > 7) return;
    if (tampon > 31 || (tampon >= 1 && tampon <= 7)) return;

    /* R4 : bit 5 = bit 2 du bloc, bits 4-0 = N tampon
     *      R5 : bit 7 = bit 0 du bloc, bit 6 = bit 1 du bloc, bits 5-0 = N octet
     *      (reprise exacte du calcul R4_bits / R5_bits de convertToZ8.py :
     *       bloc 3 -> R4 = $00|t, R5 = $C0|o ; bloc 4 -> R4 = $20|t, R5 = $00|o ;
     *       bloc 5 -> $20|$80 ; bloc 6 -> $20|$40 ; bloc 7 -> $20|$C0)          */
    r4 = (uint8_t)(((bloc & 4) << 3) | tampon);
    r5 = (uint8_t)(((bloc & 1) << 7) | ((bloc & 2) << 5));

    for (k = 0; k < 40; k++) {
        vdp_write(SEL_R1, data40[k]);
        vdp_write(SEL_R4, r4);
        vdp_write(SEL_R5, (uint8_t)(r5 + k));
        vdp_write(EXEC, CMD_OCT);   /* $34 : R1 -> octet pointe par R4/R5 */
    }
}

/* Code caractere R1 pour l affichage : caractere n (0..3) du tampon.
 *  Tampon 0 -> codes 0..3 ; tampon 8..31 -> codes 32..127.              */
uint8_t spr_code(uint8_t tampon, uint8_t n) /*__z88dk_callee*/
{
    if (tampon == 0) return n;
    return (uint8_t)(32 + 4 * (tampon - 8) + n);
}

/* Affiche le caractere code du bloc donne (3 a 7), avec sa palette
 *  R3, a la colonne x et la ligne y.                              */
void vdp_put_quad_bloc(uint8_t bloc, uint8_t code, uint8_t attr, uint8_t x, uint8_t y) /*__z88dk_callee*/
{
    vdp_write(SEL_R1, code);
    vdp_write(SEL_R2, R2_QUAD_BLOC[bloc]);
    vdp_write(SEL_R6, vdp_row(y));
    vdp_write(SEL_R7, x);
    vdp_write(SEL_R3, attr);
    vdp_write(EXEC, CMD_KRF);
}

void unpack_bloc(uint8_t v, uint8_t *bloc, uint8_t *tampon)
{
    *bloc   = (uint8_t)(v >> 5);
    *tampon = (uint8_t)(v & 0x1F);
}

int main(void)
{
    spr_init();


    #include "buffers.h"
    #include "redef.h"

    for(;;);
}
