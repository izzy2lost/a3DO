/*
 * opera-boot (GPL-2.0-only): render the no-disc banner on the build host.
 *
 * Compiles the real src/banner.c with the framebuffer and the ROM logo slot
 * pointed at host memory, runs BannerLogo(), and writes the 320x240 screen
 * as a PPM. Used to check the logo data and the drawing code without the
 * ARM toolchain or an emulator.
 *
 *   python3 tools/gen_caption.py 2026-11-20 /tmp/banner_caption.h
 *   cc -I/tmp -Isrc -DBANNER_HOST \
 *      test/banner_host.c src/banner.c -o /tmp/banner_host
 *   /tmp/banner_host build/opera-boot.bin out.ppm
 *
 * The first argument is a ROM image (the logo is read from offset 0x10000)
 * or a bare logo.bin.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char  u8;
typedef unsigned short u16;

#define W 320
#define H 240

static u16 fb_storage[W * H];
static u8  rom[131072];

u16      *host_fb   = fb_storage;
const u8 *host_logo = rom;

void BannerLogo(void);

int main(int argc, char **argv)
{
	FILE *f;
	long n;
	int x, y;

	if (argc != 3) {
		fprintf(stderr, "usage: %s rom-or-logo.bin out.ppm\n", argv[0]);
		return 2;
	}
	f = fopen(argv[1], "rb");
	if (!f) { perror(argv[1]); return 1; }
	n = (long)fread(rom, 1, sizeof rom, f);
	fclose(f);
	if (n == (long)sizeof rom)
		host_logo = rom + 0x10000;

	BannerLogo();

	f = fopen(argv[2], "wb");
	if (!f) { perror(argv[2]); return 1; }
	fprintf(f, "P6\n%d %d\n255\n", W, H);
	for (y = 0; y < H; y++) {
		for (x = 0; x < W; x++) {
			/* LRFORM: line pairs of 640 u16s, odd lines interleaved. */
			u16 c = fb_storage[(y >> 1) * (W * 2) + (y & 1) + x * 2];
			u8 rgb[3];
			rgb[0] = (u8)(((c >> 10) & 31) * 255 / 31);
			rgb[1] = (u8)(((c >> 5) & 31) * 255 / 31);
			rgb[2] = (u8)((c & 31) * 255 / 31);
			fwrite(rgb, 1, 3, f);
		}
	}
	fclose(f);
	return 0;
}
