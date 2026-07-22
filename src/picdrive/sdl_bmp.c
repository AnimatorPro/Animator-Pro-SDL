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

#include "sdl_pdr.h"

/*----------------------------------------------------------------------------
 * Setup rex & pdr interface structures...
 *--------------------------------------------------------------------------*/
char bmp_long_description[] =
	"SDL_Image Loader: BMP Files\n\n"
	"All BMP files are saved as 8-bit.";


SDL_PDR_CREATE(BMP, "BMP Format (SDL)", bmp_long_description, "bmp", SDL_LoadBMP, SDL_SaveBMP);
