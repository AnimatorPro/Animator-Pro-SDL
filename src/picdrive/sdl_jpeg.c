/*****************************************************************************
 * sdl_jpeg.c - picdriver for loading JPEG files through SDL_Image
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
#include "sdl_pdr.h"

// ---------------------------------------------------------------------------
/*
 * Wrapper for SDL_Image's IMG_SaveJPG function that passes in
 * the quality parameter from the vs.pic_save_quality variable.
 */
static bool sdlpdr_save_jpeg(SDL_Surface* surface, char* path)
{
	return IMG_SaveJPG(surface, path, vs.pic_save_quality);
}

/*----------------------------------------------------------------------------
 * Setup rex & pdr interface structures...
 *--------------------------------------------------------------------------*/
static char long_description[] =
	"SDL_Image Loader: JPEG Files\n\n"
	"All JPEG files are saved as 24-bit.\n"
	"JPEGs are quantized to 256 colors\n"
	"at load time.";


SDL_PDR_CREATE(JPEG, "JPEG Format (SDL)", long_description, "jpg;jpeg", IMG_Load, sdlpdr_save_jpeg);
