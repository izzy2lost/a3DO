/*
 * opera-boot (GPL-2.0-only): clean-room minimal 3DO bootstrapper.
 *
 * banner.c: the no-disc banner.  An original a3DO design: the word
 * "a3DO" in bold 7x9 glyphs (white, red, gold, blue) over a matching
 * red/gold/blue rule, plus the build caption.  All artwork here is
 * original to this file; no third-party logo data is used.
 * Modified for a3DO (2026-11-19): replaced the upstream third-party
 * 3DO logo emblem and wordmark.
 * Linked only into the launch image and painted once at cold boot;
 * the retry loop does not repaint.
 *
 * Framebuffer model: 320x240 16bpp at dram_size+VRAM_BYTES-256K, in the
 * MADAM LRFORM line-pair interleave: pixel (x,y) lives at u16 index
 * (y>>1)*640 + x*2 + (y&1), matching the ROM-installed startup VDL.
 */
#include "opera-boot.h"

#include "banner_caption.h"

#define SCREEN_WIDTH   320
#define SCREEN_HEIGHT  240
#define SCREEN_DEPTH   16
#define SCREEN_BPL     (SCREEN_WIDTH * (SCREEN_DEPTH / 8))
#define VRAM_BYTES     (1024 * 1024)

/* Wordmark geometry: four 7x9 glyphs at 4x scale, one 6px gap apart. */
#define GLYPH_W        7
#define GLYPH_H        9
#define GLYPH_COUNT    4
#define MARK_SCALE     4
#define MARK_GAP       6
#define MARK_W         ((GLYPH_COUNT * GLYPH_W * MARK_SCALE) + \
                        ((GLYPH_COUNT - 1) * MARK_GAP))
#define MARK_X         ((SCREEN_WIDTH - MARK_W) / 2)
#define MARK_Y         72
#define RULE_Y         (MARK_Y + (GLYPH_H * MARK_SCALE) + 14)
#define RULE_H         6

/* RGB555 colours, matching the a3DO interface theme. */
#define COLOR_WHITE    0x7FFF
#define COLOR_RED      0x7085   /* #E3262B */
#define COLOR_GOLD     0x7EE3   /* #FCBD1A */
#define COLOR_BLUE     0x21FF   /* #4578FA */

#define CAPTION_X      ((SCREEN_WIDTH - CAPTION_WIDTH) / 2)
#define CAPTION_Y      194
#define CAPTION_COLOR  0x7FFF

/* Original 7x9 1bpp glyphs for "a3DO", MSB-first per row (top 7 bits),
 * drawn with two-pixel strokes. */
static const u8 mark_rows[GLYPH_COUNT][GLYPH_H] =
  {
    { 0x00, 0x00, 0x7C, 0xFE, 0x06, 0x7E, 0xC6, 0xFE, 0x76 },   /* a */
    { 0xFC, 0xFE, 0x06, 0x3C, 0x3E, 0x06, 0x06, 0xFE, 0xFC },   /* 3 */
    { 0xF8, 0xFC, 0xCE, 0xC6, 0xC6, 0xC6, 0xCE, 0xFC, 0xF8 },   /* D */
    { 0x7C, 0xFE, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xFE, 0x7C }    /* O */
  };

static const u16 mark_colors[GLYPH_COUNT] =
  {
    COLOR_WHITE, COLOR_RED, COLOR_GOLD, COLOR_BLUE
  };

static const u16 rule_colors[3] =
  {
    COLOR_RED, COLOR_GOLD, COLOR_BLUE
  };

static u32 banner_dram_size(void)
{
	u32 msys = MADAM_MSYS;
	u32 b0 = 1u << (((msys >> 4) & 6));
	u32 b1 = 1u << (((msys >> 2) & 6));
	return ((b0 >> 2) + (b1 >> 2)) << 20;
}

static u16 *banner_fb(void)
{
	return (u16 *)(banner_dram_size() + VRAM_BYTES - 256 * 1024);
}

static void fill16(u16 *p, u16 v, u32 words)
{
	while (words--)
		*p++ = v;
}

/* LRFORM-addressed pixel run writer: a line pair is SCREEN_BPL u16s
 * (640); even line = low halfword, odd line = high halfword. */
static void span16(u16 *fb, u32 x, u32 y, u32 n, u16 color)
{
	u32 px = (y >> 1) * SCREEN_BPL + (y & 1) + x * 2;
	while (n-- > 0) {
		fb[px] = color;
		px += 2;
	}
}


static
void
_draw_caption_row(u16      *fb_,
                  const u8 *row_,
                  u32       y_)
{
  u32 x;
  u32 run = 0;

  for(x = 0; x < CAPTION_WIDTH; x++)
    {
      if((row_[(x >> 3)] & (0x80 >> (x & 7))) != 0)
        run++;
      else if(run != 0)
        {
          span16(fb_, ((CAPTION_X + x) - run), y_, run, CAPTION_COLOR);
          run = 0;
        }
    }

  if(run != 0)
    span16(fb_, ((CAPTION_X + CAPTION_WIDTH) - run), y_, run, CAPTION_COLOR);
}


// Startup banner: black with the centred a3DO wordmark, painted once.
void
BannerLogo(void)
{
  u16 *fb = banner_fb();
  int g;
  int r;
  int c;
  int sy;
  u32 third;

  fill16(fb, 0, (((u32)SCREEN_BPL * SCREEN_HEIGHT) / 2));

  /* Wordmark: each set glyph cell becomes a MARK_SCALE square. */
  for(g = 0; g < GLYPH_COUNT; g++)
    {
      u32 gx = MARK_X + (u32)(g * ((GLYPH_W * MARK_SCALE) + MARK_GAP));

      for(r = 0; r < GLYPH_H; r++)
        {
          for(c = 0; c < GLYPH_W; c++)
            {
              if((mark_rows[g][r] << c) & 0x80)
                {
                  for(sy = 0; sy < MARK_SCALE; sy++)
                    span16(fb,
                           (gx + (u32)(c * MARK_SCALE)),
                           (MARK_Y + (u32)((r * MARK_SCALE) + sy)),
                           MARK_SCALE,
                           mark_colors[g]);
                }
            }
        }
    }

  /* Red, gold and blue rule under the wordmark, in equal thirds. */
  third = MARK_W / 3;
  for(sy = 0; sy < RULE_H; sy++)
    for(g = 0; g < 3; g++)
      span16(fb,
             (MARK_X + (u32)g * third),
             (RULE_Y + (u32)sy),
             (g == 2) ? (MARK_W - (2 * third)) : third,
             rule_colors[g]);

  for(r = 0; r < CAPTION_HEIGHT; r++)
    _draw_caption_row(fb, caption_rows[r], (CAPTION_Y + (u32)r));
}
