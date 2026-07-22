/*****************************************************************************
 * sdl_pdr.c - shared code for the SDL_Image picdrivers
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

#include "jimk.h"
#include "rcel.h"
#include "sdl_pdr.h"

/*----------------------------------------------------------------------------
 * PDR_SdlFile structure, our extension to host's Image_file.
 * Since all the file IO is handled through SDL_image, I'm trying
 * to keep the bulk of what's here generic.  Instead of doing picdriver
 * code per format, I've written functions that take pointers to the IMG_*
 * functions.
 *--------------------------------------------------------------------------*/
typedef struct pdr_sdlfile {
	Image_file hdr; /* PJ Image_file, must be first in struct! */
	SDL_Surface* surface;
	int width;
	int height;
	int depth;
	char* path[PATH_MAX];
	SDL_Surface* (*sdl_load_image)(char* path);
	bool (*sdl_save_image)(SDL_Surface* surface, char* path);
} PDR_SdlFile;

/*****************************************************************************
 * as long as it's one frame of 8 bits-per-pixel, we can do it as requested.
 * if the host has asked for anything else, force the data in the Anim_info
 * to match what we can do, and return FALSE to indicate we tweaked it.
 ****************************************************************************/
bool sdlpdr_spec_best_fit(Anim_info* ainfo)
{
	(void)ainfo;
	return true;
}

/*****************************************************************************
 * Clean up resources used by picture driver.
 ****************************************************************************/
void sdlpdr_close_file(Image_file** psf)
{
	PDR_SdlFile* sf;

	if (psf == NULL || *psf == NULL) { /* these could be NULL if we get   */
		return;                        /* called by our own error cleanup.*/
	} else {
		sf = (PDR_SdlFile*)*psf; /* retrieve and recast to our type */
	}

	if (sf->surface != NULL) { /* if file is open, 			   */
		SDL_DestroySurface(sf->surface);
		sf->surface = NULL;
	}

	free(sf);    /* free our control structure	   */
	*psf = NULL; /* indicate it's now free          */
}

/*****************************************************************************
 * allocate main data structure, open file.
 ****************************************************************************/
Errcode sdlpdr_alloc_and_open(PDR_SdlFile** psf, char* path, char* openmode)
{
	(void)path;
	(void)openmode;

	// Alloc storage, punt on error.
	PDR_SdlFile* sf = (PDR_SdlFile*)calloc(1, sizeof(PDR_SdlFile));

	if (sf == NULL) {
		return Err_no_memory;
	}

	// Copy over the full path for both reads and writes
	strncpy(sf->path, path, PATH_MAX);
	*psf = sf;

	/*
	 * kiki note:
	 * Old system would open a FILE* handle here for whatever was
	 * needed. We're not doing that anymore.
	 */

	return Success;
}

/*****************************************************************************
 * Open up the file, verify file header.
 ****************************************************************************/
Errcode sdlpdr_open_file(Pdr* pd, char* path, Image_file** pif, Anim_info* ainfo)
{
	Errcode err;
	PDR_SdlFile* sf;

	assert(pd->sdl_load_image);

	sf = NULL;   /* preset these to NULL in case we take our 	*/
	*pif = NULL; /* error exit path before everything's alloc'd. */

	/*------------------------------------------------------------------------
	 * allocate main data structure, open file.
	 *----------------------------------------------------------------------*/

	err = sdlpdr_alloc_and_open(&sf, path, "rb");
	if (err != Success) {
		sdlpdr_close_file((Image_file**)&sf);
		return err;
	}

	/*------------------------------------------------------------------------
	 * at this point, analyze the contents of the file to make sure it
	 * contains data in the format you expected.  there is no guarantee that
	 * the file is the type you expect -- in fact, quite often it won't be.
	 * if you don't recognize the file format, return Err_pic_unknown.
	 *----------------------------------------------------------------------*/

	if (sf->surface) {
		SDL_DestroySurface(sf->surface);
		sf->surface = NULL;
	}

	/*
	 * This calls the specific load function in each PDR, but allows
	 * the rest of the code to be more generic.
	 */
	sf->surface = pd->sdl_load_image(path);

	if (!sf->surface) {
		sdlpdr_close_file((Image_file**)&sf);
		return Err_pic_unknown;
	}

	sf->width = sf->surface->w;
	sf->height = sf->surface->h;

	// Fix the logic for determining the depth
	sf->depth = (sf->surface->format == SDL_PIXELFORMAT_INDEX8) ? 8 : 24;

	/*------------------------------------------------------------------------
	 * fill in values we return to the host via pointers.
	 *----------------------------------------------------------------------*/
	memset(ainfo, 0, sizeof(*ainfo));
	ainfo->width = sf->surface->w;
	ainfo->height = sf->surface->h;
	ainfo->depth = sf->depth;
	ainfo->num_frames = 1;
	ainfo->millisec_per_frame = DEFAULT_AINFO_SPEED;

	*pif = (Image_file*)sf;
	return Success;
}

/*****************************************************************************
 * read in 1st (er, only) image.
 ****************************************************************************/
Errcode sdlpdr_read_first(Image_file* ifile, Rcel* screen)
{
	PDR_SdlFile* sf = (PDR_SdlFile*)ifile;
	SDL_Palette* screen_palette = NULL;
	assert(sf);
	assert(sf->surface);

	//! TODO: handle color remapping
	if (sf->surface->format != SDL_PIXELFORMAT_INDEX8) {
		SDL_Palette* screen_palette = SDL_CreatePalette(screen->cmap->num_colors);
		if (!screen_palette) {
			fprintf(stderr, "[sdlpdr_read_first] unable to create palette: %s\n", SDL_GetError());
			sdlpdr_close_file((Image_file**)&sf);
			return Err_no_memory;
		}

		sdlpdr_cmap_to_palette(screen->cmap, screen_palette);

		if (vs.pic_auto_fit_palette == PIC_IO_PAL_FIT) {
			SDL_Surface* temp = sdlpdr_convert_surface_to_palette(sf->surface, screen_palette);
			if (!temp) {
				fprintf(stderr, "[sdlpdr_open_file] unable to fit to palette.\n");
				SDL_DestroyPalette(screen_palette);
				sdlpdr_close_file((Image_file**)&sf);
				return Err_no_memory;
			}
			SDL_DestroySurface(sf->surface);
			sf->surface = temp;
		} else {  // PIC_IO_PAL_OVERWRITE
			SDL_Surface* temp = sdlpdr_convert_colors(sf->surface, 256);
			if (!temp) {
				fprintf(stderr, "[sdlpdr_open_file] unable to convert high color to indexed: %s\n",
						SDL_GetError());
				sdlpdr_close_file((Image_file**)&sf);
				return Err_no_memory;
			}

			SDL_DestroySurface(sf->surface);
			sf->surface = temp;
		}
	}

	/* read pixels by index for now */
	if (!SDL_LockSurface(sf->surface)) {
		fprintf(stderr, "Unable to lock surface for file load: %s\n", SDL_GetError());
		return Err_no_lock;
	}

	uint8_t* surface_buf = (uint8_t*)sf->surface->pixels;
	uint8_t buf[sf->width];

	for (int y = 0; y < sf->height; y += 1) {
		for (int x = 0; x < sf->width; x += 1) {
			buf[x] = surface_buf[y * sf->surface->pitch + x];
		}
		pj_put_hseg(screen, buf, 0, y, sf->width);
	}

	SDL_UnlockSurface(sf->surface);

	/* copy cmap */
	SDL_Palette* pal = SDL_GetSurfacePalette(sf->surface);
	if (pal == NULL) {
		fprintf(stderr, "Unable to get palette from loaded image: %s\n", SDL_GetError());
		return Err_no_palette;
	}

	sdlpdr_palette_to_cmap(pal, screen->cmap);

	return Success;
}

/*****************************************************************************
 * create an output file (alloc, open, and write file header).
 ****************************************************************************/
Errcode sdlpdr_create_file(Pdr* pd, char* path, Image_file** pif, Anim_info* ainfo)
{
	Errcode err;
	PDR_SdlFile* sf;

	assert(pd->sdl_save_image);


	sf = NULL;
	*pif = NULL;

	/*------------------------------------------------------------------------
	 * allocate main data structure, open file.
	 *----------------------------------------------------------------------*/

	err = sdlpdr_alloc_and_open(&sf, path, "wb");
	if (err != Success) {
		sdlpdr_close_file((Image_file**)&sf);
		return err;
	}

	/*------------------------------------------------------------------------
	 * save what we need out of anim_info, and return success...
	 *----------------------------------------------------------------------*/

	sf->sdl_save_image = pd->sdl_save_image;
	sf->width = ainfo->width;
	sf->height = ainfo->height;

	*pif = (Image_file*)sf;
	return Success;
}

/*****************************************************************************
 * save screen image.
 ****************************************************************************/
Errcode sdlpdr_save_frames(Image_file* ifile, Rcel* screen, int num_frames,
						   Errcode (*seek_frame)(int ix, void* seek_data), void* seek_data,
						   Rcel* work_screen)
{
	(void)num_frames;
	(void)seek_frame;
	(void)seek_data;
	(void)work_screen;

	PDR_SdlFile* sf = (PDR_SdlFile*)ifile;

	/*------------------------------------------------------------------------
	 * write the picture file from the input screen, using output options.
	 *----------------------------------------------------------------------*/

	int y;
	int w = sf->width;
	int h = sf->height;

	sf->surface = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_INDEX8);
	if (!sf->surface) {
		sdlpdr_close_file((Image_file**)&sf);
		return Err_no_surface;
	}

	/*
	 * Copy over palette
	 */

	SDL_Palette* palette = SDL_CreatePalette(screen->cmap->num_colors);
	if (!palette) {
		SDL_DestroySurface(sf->surface);
		sdlpdr_close_file((Image_file**)&sf);
		return Err_no_palette;
	}

	sdlpdr_cmap_to_palette(screen->cmap, palette);

	palette->ncolors = screen->cmap->num_colors;

	SDL_SetSurfacePalette(sf->surface, palette);
	SDL_DestroyPalette(palette);
	palette = NULL;

	Pixel* buf = malloc(w + 2);
	if (!buf) {
		SDL_DestroySurface(sf->surface);
		sdlpdr_close_file((Image_file**)&sf);
		return Err_no_memory;
	}

	if (!SDL_LockSurface(sf->surface)) {
		SDL_DestroySurface(sf->surface);
		sdlpdr_close_file((Image_file**)&sf);
		return Err_no_lock;
	}

	uint8_t* surface_buf = (uint8_t*)sf->surface->pixels;

	for (y = 0; y < h; y += 1) {
		pj_get_hseg(screen, buf, 0, y, w);
		for (int x = 0; x < w; x += 1) {
			surface_buf[y * w + x] = buf[x];
		}
	}

	SDL_UnlockSurface(sf->surface);

	/*
	 * Write Image
	 * This calls the specific save function in each PDR, but allows
	 * the rest of the code to be more generic.
	 */
	if (!sf->sdl_save_image(sf->surface, sf->path)) {
		sdlpdr_close_file((Image_file**)&sf);
		soft_continu_box("!%s", "sdl_cant_write", sf->path);
		return Err_file_access;
	}

	return Success;
}
