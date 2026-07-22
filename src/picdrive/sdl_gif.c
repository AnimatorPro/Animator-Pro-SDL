/*****************************************************************************
 * sdl_gif.c - picdriver for loading GIF files through SDL_Image
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

#include "gif_lib.h"

#include "jimk.h"
#include "sdl_pdr.h"

// ---------------------------------------------------------------------------
static bool sdlpdr_gif_spec_best_fit(Anim_info* ainfo)
{
	if (ainfo->depth == 8 && ainfo->num_frames <= MAXFRAMES) {
		return true;
	}

	ainfo->depth = 8;
	if (ainfo->num_frames > MAXFRAMES) {
		ainfo->num_frames = MAXFRAMES;
	}

	return false;
}

// ---------------------------------------------------------------------------
static SDL_Surface* sdlpdr_load_gif(const char* path)
{
	GifFileType* gif = NULL;
	SDL_Surface* surface = NULL;
	int error;

	// Open the GIF file
	gif = DGifOpenFileName(path, &error);
	if (gif == NULL) {
		return NULL;
	}

	// Read the entire GIF file into memory
	if (DGifSlurp(gif) != GIF_OK) {
		DGifCloseFile(gif, &error);
		return NULL;
	}

	// Ensure there's at least one image
	if (gif->ImageCount < 1 || gif->SavedImages == NULL) {
		DGifCloseFile(gif, &error);
		return NULL;
	}

	// Get the first saved image
	SavedImage* image = &gif->SavedImages[0];

	// Get the appropriate color map (local or global)
	ColorMapObject* colorMap =
		image->ImageDesc.ColorMap ? image->ImageDesc.ColorMap : gif->SColorMap;

	if (colorMap == NULL) {
		DGifCloseFile(gif, &error);
		return NULL;
	}

	int width = image->ImageDesc.Width;
	int height = image->ImageDesc.Height;

	// Create a new surface with INDEX8 format
	surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_INDEX8);
	if (surface == NULL) {
		DGifCloseFile(gif, &error);
		return NULL;
	}

	// Create a palette and set it to the surface
	SDL_Palette* palette = SDL_CreatePalette(colorMap->ColorCount);
	if (palette == NULL) {
		SDL_DestroySurface(surface);
		DGifCloseFile(gif, &error);
		return NULL;
	}

	// Fill the palette with colors from the GIF
	for (int i = 0; i < colorMap->ColorCount; i++) {
		SDL_Color color = {
			.r = colorMap->Colors[i].Red,
			.g = colorMap->Colors[i].Green,
			.b = colorMap->Colors[i].Blue,
			.a = 0xFF  // GIF doesn't store alpha, so set to opaque
		};
		SDL_SetPaletteColors(palette, &color, i, 1);
	}

	// Set the palette to the surface
	// Note: Surface now holds a pointer to the palette
	// so don't free it
	SDL_SetSurfacePalette(surface, palette);

	// Copy the image data to the surface
	SDL_LockSurface(surface);

	Uint8* dest = (Uint8*)surface->pixels;
	GifByteType* src = image->RasterBits;
	int pitch = surface->pitch;

	for (int y = 0; y < height; y++) {
		memcpy(dest, src, width);
		dest += pitch;
		src += width;
	}

	SDL_UnlockSurface(surface);

	// Close the GIF file
	DGifCloseFile(gif, &error);

	return surface;
}

// ---------------------------------------------------------------------------
static bool sdlpdr_save_gif(SDL_Surface* surface, const char* path)
{
	GifFileType* gif = NULL;
	ColorMapObject* colorMap = NULL;
	bool success = false;
	int error;
	const bool write_alpha = vs.pic_write_alpha == PIC_IO_WRITE_ALPHA;

	// Check that we have an 8-bit indexed surface with palette
	SDL_Palette* palette = SDL_GetSurfacePalette(surface);
	if (surface->format != SDL_PIXELFORMAT_INDEX8 || !palette) {
		return false;
	}

	// Create a GIF color map from the SDL palette
	const int numColors = palette->ncolors;
	colorMap = GifMakeMapObject(numColors, NULL);
	if (!colorMap) {
		return false;
	}

	// Convert SDL palette to GIF color map
	for (int i = 0; i < numColors; i++) {
		colorMap->Colors[i].Red = palette->colors[i].r;
		colorMap->Colors[i].Green = palette->colors[i].g;
		colorMap->Colors[i].Blue = palette->colors[i].b;
	}

	// Create the GIF file
	gif = EGifOpenFileName(path, false, &error);
	if (!gif) {
		GifFreeMapObject(colorMap);
		return false;
	}

	// Set the screen descriptor
	if (EGifPutScreenDesc(gif, surface->w, surface->h, 8, 0, colorMap) != GIF_OK) {
		goto cleanup;
	}

	// Add transparency for index 0 when requested
	if (write_alpha) {
		// Graphics Control Extension block
		const uint8_t gce_block[4] = {
			0x01,        // Packed field: 1=transparency enabled
			0x00, 0x00,  // Delay time (not used for still images)
			0x00         // Transparent color index (0)
		};

		// Write the Graphics Control Extension
		if (EGifPutExtension(gif, GRAPHICS_EXT_FUNC_CODE, 4, gce_block) != GIF_OK) {
			goto cleanup;
		}
	}

	// Set the image descriptor (full screen, no local color map)
	if (EGifPutImageDesc(gif, 0, 0, surface->w, surface->h, false, NULL) != GIF_OK) {
		goto cleanup;
	}

	// Lock the surface to access pixel data
	SDL_LockSurface(surface);

	// Write the image data line by line
	GifPixelType* line = (GifPixelType*)surface->pixels;
	int pitch = surface->pitch;

	for (int y = 0; y < surface->h; y++) {
		if (EGifPutLine(gif, line, surface->w) != GIF_OK) {
			goto cleanup;
		}
		line = (GifPixelType*)((uint8_t*)line + pitch);
	}

	// Unlock the surface if it was locked

	success = true;

cleanup:
	SDL_UnlockSurface(surface);

	// Close the GIF file
	if (gif && EGifCloseFile(gif, &error) != GIF_OK) {
		success = false;
	}

	// The color map is now owned by the GIF file, so we don't free it separately
	return success;
}

/*----------------------------------------------------------------------------
 * Setup rex & pdr interface structures...
 *--------------------------------------------------------------------------*/
static char long_description[] =
	"GIF File IO (giflib)\n\n"
	"All GIF files are saved as 8-bit.";


SDL_PDR_CREATE_ANIM(GIF, "GIF Format (giflib)", long_description, "gif", sdlpdr_load_gif,
					sdlpdr_save_gif, sdlpdr_gif_spec_best_fit);
