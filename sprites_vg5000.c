/* ============================================================================
 *  sprites_vg5000.c — Gestionnaire de sprites pour VG5000u (Z80 + EF9345)
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
 *   - Le rafraichissement du buffer BASIC ($4000) est coupe :
 *     interruptions masquees (di, comme Vg5kGraphics) et $47FD = 0
 *     (astuce rendomizer). Ne jamais le laisser tourner : il
 *     ecraserait l'affichage direct (forum system-cfg).
 *
 *  Compilation (z88dk, SCCZ80 recommande) :
 *      zcc +vg5k -O3 -create-app -o demo sprites_vg5000.c
 *      -> demo.k7 : CLOAD dans DCVG5K (dcvg5k.free.fr), sinon CALL 20480.
 *
 *  Sources : projet Vg5kGraphics (rodolphe74) et son convertToZ8.py ;
 *  rendomizer (vg5000.webnode.fr) ; ROM commentee Triceraprog.
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

/* Mode R2 d'un caractere quadrichrome de la banque Q3 */
#define R2_QUAD_Q3      0xD9

/* Palettes R3 : bit (7-c) = couleur c
   (0 blanc, 1 cyan, 2 magenta, 3 bleu, 4 jaune, 5 vert, 6 rouge, 7 noir) */
#define ATTR_BLACK      0x01        /* { noir }                         */
#define ATTR_BALL       0x03        /* { noir, rouge }                  */
#define ATTR_WHITE      0x80        /* { blanc }                        */

/* ------------------------------------------------------------------ */
/* Interfacage avec les routines ROM (thunks assembleur)              */
/* ------------------------------------------------------------------ */

extern void vdp_set(const uint8_t *de);
extern void vdp_di(void);

#asm
; --- vdp_set(const uint8_t *de) ---------------------------------------
vdp_set::
_vdp_set::
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

; --- vdp_di() ----------------------------------------------------------
vdp_di::
_vdp_di::
    di
    ret
#endasm

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

void spr_redef_tampon(uint8_t r4, uint8_t r5, const uint8_t *data40)
{
    uint8_t k;
    vdp_write(SEL_R4, r4);
    for (k = 0; k < 40; k++) {
        vdp_write(SEL_R1, data40[k]);
        vdp_write(SEL_R5, (uint8_t)(r5 + k));
        vdp_write(EXEC, CMD_OCT);
    }
}

void vdp_put_quad(uint8_t code, uint8_t attr, uint8_t x, uint8_t y)
{
    vdp_write(SEL_R1, code);
    vdp_write(SEL_R2, R2_QUAD_Q3);
    vdp_write(SEL_R6, vdp_row(y));
    vdp_write(SEL_R7, x);
    vdp_write(SEL_R3, attr);
    vdp_write(EXEC, CMD_KRF);
}

void fill_row(uint8_t y, uint8_t code, uint8_t attr)
{
    uint8_t i;
    vdp_write(SEL_R1, code);
    vdp_write(SEL_R2, R2_QUAD_Q3);
    vdp_write(SEL_R3, attr);
    vdp_write(SEL_R0, 0x01);
    vdp_write(SEL_R7, 0);
    for (i = 0; i < SCREEN_W; i++) {
        vdp_write(EXEC_KRF_ROW, vdp_row(y));
    }
    vdp_write(SEL_R0, 0x00);
}

/* ------------------------------------------------------------------ */
/* Couche 2 : carte logique de l'ecran                                */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t code;
    uint8_t attr;
} cell_t;

static cell_t lmap[SCREEN_H][SCREEN_W];

void buf_put(uint8_t code, uint8_t attr, uint8_t x, uint8_t y)
{
    if (x >= SCREEN_W || y >= SCREEN_H) return;
    lmap[y][x].code = code;
    lmap[y][x].attr = attr;
}

/* ------------------------------------------------------------------ */
/* Couche 3 : gestionnaire de sprites                                 */
/* ------------------------------------------------------------------ */

#define SPR_MAX_CELLS  4

typedef struct {
    uint8_t x, y;
    uint8_t w, h;
    uint8_t base;
    uint8_t frames;
    uint8_t color;
    uint8_t visible;
    uint8_t bgcode[SPR_MAX_CELLS];
    uint8_t bgattr[SPR_MAX_CELLS];
} sprite_t;

void spr_show(sprite_t *s, uint8_t f)
{
    uint8_t cx, cy, i = 0;
    if (s->visible) return;
    if ((uint16_t)s->w * s->h > SPR_MAX_CELLS) return;
    for (cy = 0; cy < s->h; cy++) {
        for (cx = 0; cx < s->w; cx++) {
            cell_t *c = &lmap[s->y + cy][s->x + cx];
            s->bgcode[i] = c->code;
            s->bgattr[i] = c->attr;
            vdp_put_quad((uint8_t)(s->base + f * (s->w * s->h) + i),
                         s->color,
                         (uint8_t)(s->x + cx), (uint8_t)(s->y + cy));
            i++;
        }
    }
    s->visible = 1;
}

void spr_hide(sprite_t *s)
{
    uint8_t cx, cy, i = 0;
    if (!s->visible) return;
    for (cy = 0; cy < s->h; cy++) {
        for (cx = 0; cx < s->w; cx++) {
            vdp_put_quad(s->bgcode[i], s->bgattr[i],
                         (uint8_t)(s->x + cx), (uint8_t)(s->y + cy));
            i++;
        }
    }
    s->visible = 0;
}

void spr_move(sprite_t *s, uint8_t x, uint8_t y, uint8_t f)
{
    spr_hide(s);
    s->x = x;
    s->y = y;
    spr_show(s, f);
}

/* ------------------------------------------------------------------ */
/* Demo : balle animee qui rebondit le long d'une piste               */
/* ------------------------------------------------------------------ */

#define BALL_CODE     32
#define BALL_FRAMES   2
#define CODE_BLANK    40

static const uint8_t gfx_ball_f0[40] = {
    0x00,0x00,0x55,0x55, 0x00,0x00,0x55,0x55, 0x50,0x01,0x55,0x55,
    0x54,0x05,0x55,0x15, 0x54,0x15,0x55,0x15, 0x55,0x15,0x54,0x15,
    0x55,0x15,0x54,0x05, 0x55,0x55,0x50,0x01, 0x55,0x55,0x00,0x00,
    0x55,0x55,0x00,0x00
};
static const uint8_t gfx_ball_f1[40] = {
    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00, 0x40,0x01,0x00,0x00, 0x54,0x05,0x00,0x00,
    0x55,0x15,0x00,0x00, 0x55,0x15,0x00,0x00, 0x55,0x15,0x00,0x00,
    0x54,0x05,0x00,0x00
};
static const uint8_t gfx_blank[40] = {
    0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
    0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0
};

static const uint8_t tri[6] = { 0, 1, 2, 3, 2, 1 };

static sprite_t ball = {
    0, 15,
    2, 2,
    BALL_CODE, BALL_FRAMES,
    ATTR_BALL,
    0,
    {0,0,0,0}, {0,0,0,0}
};

void delay(uint16_t n)
{
    volatile uint16_t i;
    for (i = 0; i < n; i++) { }
}

int main(void)
{
    uint8_t x;
    uint8_t y;

    spr_init();

    spr_redef_tampon(0x08, 0xC0, gfx_ball_f0);
    spr_redef_tampon(0x09, 0xC0, gfx_ball_f1);
    spr_redef_tampon(0x0A, 0xC0, gfx_blank);

    for (y = 0; y < SCREEN_H; y++) {
        fill_row(y, CODE_BLANK, ATTR_BLACK);
        for (x = 0; x < SCREEN_W; x++) {
            buf_put(CODE_BLANK, ATTR_BLACK, x, y);
        }
    }

    fill_row(20, CODE_BLANK, ATTR_WHITE);
    for (x = 0; x < SCREEN_W; x++) {
        buf_put(CODE_BLANK, ATTR_WHITE, x, 20);
    }

    for (;;) {
        for (x = 0; x < (SCREEN_W - ball.w); x++) {
            y = (uint8_t)(15 + tri[x % 6]);
            spr_move(&ball, x, y, (uint8_t)((x >> 1) & 1));
            delay(1000);
        }
    }
}