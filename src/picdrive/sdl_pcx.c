/*****************************************************************************
 * sdl_bmp.c - picdriver for loading BMP files through SDL_Image
 ****************************************************************************/

/*----------------------------------------------------------------------------
 * include the usual header files...
 *--------------------------------------------------------------------------*/

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <assert.h>
#include <memory.h>
#include <reqlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pcx/pcx.h"
#include "sdl_pdr.h"

#define PCX_MAX_RUN 63 /* longest run (since hi 2 bits are used elsewhere */
#define PCX_CMAP_MAGIC 12

// ----------------------------------------------------------------------------
/* Do PCX compression of buf into file out */
static void pcx_comp_buf(XFILE* out, UBYTE* buf, int count)
{
	int same_count, lcount;
	int c;

	same_count = 0;
	while ((count -= same_count) > 0) {
		if ((lcount = count) > PCX_MAX_RUN) {
			lcount = PCX_MAX_RUN;
		}
		if ((same_count = pj_bsame(buf, lcount)) > 1) {
			xfputc(same_count | 0xc0, out);
			xfputc(*buf, out);
			buf += same_count;
		} else {
			c = *buf;
			if ((c & 0xc0) == 0xc0) {
				xfputc(0xc1, out);
				xfputc(c, out);
				buf += 1;
			} else {
				xfputc(c, out);
				buf += 1;
			}
		}
	}
}

// ----------------------------------------------------------------------------
static bool sdlpdr_save_pcx(SDL_Surface* surface, const char* path)
{
	Pcx_header rhdr;
	UBYTE* buf = NULL;
	Errcode err = Success;
	int width, height, i;
	XFILE* out = NULL;

	/* Check if the surface is in INDEX8 format */
	if (surface->format != SDL_PIXELFORMAT_INDEX8) {
		return false;
	}

	/* Get dimensions */
	width = surface->w;
	height = surface->h;

	/* Open output file */
	err = xffopen(path, &out, XWRITEONLY);
	if (err != Success) {
		return false;
	}

	/* Set up header */
	clear_struct(&rhdr);
	rhdr.magic = 10;
	rhdr.version = 5;
	rhdr.encode = 1;
	rhdr.bitpx = 8;
	rhdr.x1 = 0;
	rhdr.y1 = 0;
	rhdr.x2 = width - 1;
	rhdr.y2 = height - 1;
	rhdr.cardw = width;
	rhdr.cardh = height;
	rhdr.nplanes = 1;
	rhdr.bpl = width;

	/* Write header */
	if (xfwrite(&rhdr, sizeof(rhdr), 1, out) < 1) {
		err = xerrno();
		goto cleanup;
	}

	/* Allocate line buffer */
	if ((buf = pj_malloc(width)) == NULL) {
		err = Err_no_memory;
		goto cleanup;
	}

	/* Lock the surface for reading */
	SDL_LockSurface(surface);

	/* Write each line of pixel data */
	for (i = 0; i < height; i++) {
		/* Get a row of pixel data */
		UBYTE* src = (UBYTE*)surface->pixels + (i * surface->pitch);
		memcpy(buf, src, width);

		/* Compress and write the line */
		pcx_comp_buf(out, buf, width);

		if (xferror(out) != 0) {
			SDL_UnlockSurface(surface);
			err = xerrno();
			goto cleanup;
		}
	}

	/* Unlock the surface */
	SDL_UnlockSurface(surface);

	/* Write colormap marker */
	xfputc(PCX_CMAP_MAGIC, out);

	/* Write palette */
	SDL_Palette* palette = SDL_GetSurfacePalette(surface);
	if (palette) {
		UBYTE rgb_palette[768]; /* 256 colors * 3 components */
		for (i = 0; i < 256 && i < palette->ncolors; i++) {
			rgb_palette[i * 3] = palette->colors[i].r;
			rgb_palette[i * 3 + 1] = palette->colors[i].g;
			rgb_palette[i * 3 + 2] = palette->colors[i].b;
		}
		xfwrite(rgb_palette, 1, 768, out);
	}

	if (xferror(out) != 0) {
		err = xerrno();
	}

cleanup:
	pj_freez(&buf);
	if (out) {
		xffclose(&out);
	}
	return err == Success;
}

/*----------------------------------------------------------------------------
 * Setup rex & pdr interface structures...
 *--------------------------------------------------------------------------*/
static char long_description[] =
	"SDL_Image Loader: PCX Files\n\n"
	"All PCX files are saved as 8-bit.";

// clang-format off
SDL_PDR_CREATE(
	PCX,
	"PCX Format (SDL)",
	long_description,
	"pcx",
	IMG_Load,
	sdlpdr_save_pcx
);
