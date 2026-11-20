/*
 * opera-boot (GPL-2.0-only): clean-room minimal 3DO bootstrapper.
 *
 * banner.c: the no-disc banner: the a3DO logo above the build caption.
 * The logo is a palettised image that mkrom places at ROM offset 0x10000
 * (tools/gen_logo.py, assets/a3do-logo.png).  If that slot does not hold
 * a valid image, an original "a3DO" wordmark in bold 7x9 glyphs (white,
 * red, gold, blue) over a red/gold/blue rule is drawn instead.  No
 * third-party logo data is used.
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

/* The logo image in ROM: "A3LG", u16 width, u16 height, 256 RGB555
 * palette entries, then one index per pixel; all big-endian.  Index 0 is
 * the background and is not drawn.  The ROM is mapped at 0x03000000, the
 * same base launch.bin is linked at. */
#ifdef BANNER_HOST
/* test/banner_host.c: draw into host memory instead of VRAM and ROM. */
extern u16      *host_fb;
extern const u8 *host_logo;
#define BANNER_LOGO_ROM host_logo
#else
#define BANNER_LOGO_ROM ((const u8 *)0x03010000)
#endif
#define LOGO_PALETTE   8
#define LOGO_PIXELS    (LOGO_PALETTE + 256 * 2)
#define LOGO_MAX_H     (CAPTION_Y - 16)

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

#ifndef BANNER_HOST
static u32 banner_dram_size(void)
{
	u32 msys = MADAM_MSYS;
	u32 b0 = 1u << (((msys >> 4) & 6));
	u32 b1 = 1u << (((msys >> 2) & 6));
	return ((b0 >> 2) + (b1 >> 2)) << 20;
}

#endif

static u16 *banner_fb(void)
{
#ifdef BANNER_HOST
	return host_fb;
#else
	return (u16 *)(banner_dram_size() + VRAM_BYTES - 256 * 1024);
#endif
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


/* Draw the ROM logo, centred above the caption.  Returns 0 (drawing
 * nothing) if the slot does not hold a usable image. */
static
int
_draw_rom_logo(u16 *fb_)
{
  const u8 *logo = BANNER_LOGO_ROM;
  const u8 *pal;
  const u8 *px;
  u32 w;
  u32 h;
  u32 x0;
  u32 y0;
  u32 x;
  u32 y;
  u32 i;

  if((logo[0] != 'A') || (logo[1] != '3') ||
     (logo[2] != 'L') || (logo[3] != 'G'))
    return 0;

  w = ((u32)logo[4] << 8) | logo[5];
  h = ((u32)logo[6] << 8) | logo[7];
  if((w == 0) || (h == 0) || (w > SCREEN_WIDTH) || (h > LOGO_MAX_H))
    return 0;

  pal = logo + LOGO_PALETTE;
  px  = logo + LOGO_PIXELS;
  x0  = (SCREEN_WIDTH - w) / 2;
  y0  = (CAPTION_Y - 8 - h) / 2;

  for(y = 0; y < h; y++)
    {
      for(x = 0; x < w; x++)
        {
          i = *px++;
          if(i != 0)
            span16(fb_, (x0 + x), (y0 + y), 1,
                   (u16)(((u32)pal[i * 2] << 8) | pal[(i * 2) + 1]));
        }
    }

  return 1;
}


// Startup banner: black with the centred a3DO logo, painted once.
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

  if(_draw_rom_logo(fb))
    {
      for(r = 0; r < CAPTION_HEIGHT; r++)
        _draw_caption_row(fb, caption_rows[r], (CAPTION_Y + (u32)r));
      return;
    }

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
