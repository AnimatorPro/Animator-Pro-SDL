//
// Created by Charles Wardlaw on 2022-10-02.
//

#include <stdbool.h>

#include <nfd.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_surface.h>

#include "pj_sdl.h"


/*--------------------------------------------------------------*/
SDL_Surface* s_surface		  = NULL;
SDL_Window* window			  = NULL;
SDL_Surface* s_window_surface = NULL;
SDL_Renderer* renderer		  = NULL;
SDL_Texture* render_target    = NULL;
SDL_Palette* vga_palette      = NULL;

// for the file requestors
static char last_path[PATH_MAX] = "";


/*--------------------------------------------------------------*/
int pj_sdl_get_video_size(LONG* width, LONG* height)
{
	if (!s_surface) {
		*width = -1;
		*height = -1;
		return 0;
	}

	*width = s_surface->w;
	*height = s_surface->h;
	return 1;
}

/*--------------------------------------------------------------*/
int pj_sdl_get_window_size(int* width, int* height)
{
	if (!window) {
		*width = -1;
		*height = -1;
		return 0;
	}

	SDL_GetWindowSize(window, width, height);
	return 1;
}

/*--------------------------------------------------------------*/
int pj_sdl_get_window_scale(float* x, float* y)
{
	LONG video_w, video_h;
	int window_w, window_h;

	*x = 1.0f;
	*y = 1.0f;

	if (!pj_sdl_get_video_size(&video_w, &video_h)) {
		fprintf(stderr, "Unable to get video size!\n");
		return 0;
	}

	if (!pj_sdl_get_window_size(&window_w, &window_h)) {
		fprintf(stderr, "Unable to get window size!\n");
		return 0;
	}

	float video_wf = video_w;
	float video_hf = video_h;
	float window_wf = window_w;
	float window_hf = window_h;

	*x = window_wf / video_wf;
	*y = window_hf / video_hf;

	return 1;
}

/*--------------------------------------------------------------*/
LONG pj_sdl_get_display_scale()
{
	return 4;
}

/*--------------------------------------------------------------*/
static SDL_FRect pj_sdl_rect_convert(const SDL_Rect* rect) {
	SDL_FRect result = { rect->x, rect->y, rect->w, rect->h };
	return result;
}

/*--------------------------------------------------------------*/
SDL_FRect pj_sdl_fit_surface(const SDL_Surface* source, const int target_w, const int target_h)
{
	SDL_Rect result_rect = {.x = 0, .y = 0, .w = source->w, .h = source->h};

	const double scale_x = ((double)target_w) / ((double)source->w);
	const double scale_y = ((double)target_h) / ((double)source->h);

	const bool can_scale_x = (source->h * scale_x) < target_h;

	if (can_scale_x) {
		result_rect.w = target_w;
		result_rect.h = source->h * scale_x;
		result_rect.y = (target_h - result_rect.h) / 2;
	} else {
		result_rect.h = target_h;
		result_rect.w = source->w * scale_y;
		result_rect.x = (target_w - result_rect.w) / 2;
	}

	return pj_sdl_rect_convert(&result_rect);
}

/*--------------------------------------------------------------*/
void pj_sdl_flip_window_surface()
{
	void* pixels = NULL;
	int pitch = 0;

	// Lock the texture so we have access to its pixels for writing
	if (!SDL_LockTexture(render_target, NULL, &pixels, &pitch)) {
		SDL_Log("Could not lock texture: %s", SDL_GetError());
		SDL_DestroyTexture(render_target);
		SDL_DestroyRenderer(renderer);
		SDL_DestroyWindow(window);
		SDL_Quit();
	}

	// Copy pixel data from surface to texture
	// memcpy(pixels, s_buffer->pixels, s_buffer->h * s_buffer->pitch);
	SDL_Surface* copy_target = SDL_CreateSurfaceFrom(
									render_target->w,
									render_target->h,
									render_target->format,
									pixels,
									pitch);

	if (!copy_target) {
		SDL_Log("Could create texture from locked pixels: %s", SDL_GetError());
	}

	SDL_BlitSurface(s_surface, NULL, copy_target, NULL);
	SDL_DestroySurface(copy_target);
	copy_target = NULL;

	// Unlock the texture-- commit changes
	SDL_UnlockTexture(render_target);

	const SDL_FRect source_rect = pj_sdl_rect_convert(&(SDL_Rect){0, 0, s_surface->w, s_surface->h});
	int window_w, window_h;
	pj_sdl_get_window_size(&window_w, &window_h);
	const SDL_FRect target_rect = pj_sdl_fit_surface(s_surface, window_w, window_h);

	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0xFF);
	SDL_RenderClear(renderer);
	SDL_RenderTexture(renderer, render_target, &source_rect, &target_rect);
	SDL_RenderPresent(renderer);
}

/*--------------------------------------------------------------*/
const char* pj_sdl_resources_path() {
	static char resources_path[PATH_MAX] = {0};
	const char* base_path = SDL_GetBasePath();

	#ifdef IS_BUNDLE
		// On macOS, resources path is very specific
		snprintf(resources_path, PATH_MAX, pj_sdl_mac_bundle_path());
	#else
		// When not in a bundle, use the resources folder next to the executable
		snprintf(resources_path, PATH_MAX, "%sresource",
			 base_path);
	#endif
	return resources_path;
}


/*--------------------------------------------------------------*/
const char* pj_sdl_preferences_path() {
	static char preferences_path[PATH_MAX] = {0};
	if (preferences_path[0] == '\0') {
		snprintf(preferences_path, PATH_MAX, SDL_GetPrefPath("skeletonheavy", "vpaint"));
		// eat the last separator
		preferences_path[SDL_strlen(preferences_path)-1] = '\0';
	}
	return preferences_path;
}


/*--------------------------------------------------------------*/
void pj_dialog_set_last_path(const char* path) {
	if (path) {
		strncpy(last_path, path, PATH_MAX);
	}
	else {
		//!TODO: Set to home folder?
		last_path[0] = '\0';
	}
}

/*--------------------------------------------------------------*/
char* pj_dialog_file_open(const char* type_name,
						  const char* extensions,
						  const char* default_path) {
	char* result = last_path;

	nfdchar_t* outPath;
	const nfdfilteritem_t filterItem = {type_name, extensions};

	nfdresult_t dialog_result = NFD_OpenDialog(&outPath, &filterItem, 1, default_path);

	if (dialog_result == NFD_OKAY) {
		strncpy(last_path, outPath, PATH_MAX);
		NFD_FreePath(outPath);
	}
	else {
		result = NULL;
	}

	NFD_Quit();
	return result;
}


/*--------------------------------------------------------------*/
char* pj_dialog_file_save(const char* type_name,
						  const char* extensions,
						  const char* default_path,
						  const char* default_name) {
	char* result = last_path;

	nfdchar_t* outPath;
	const nfdfilteritem_t filterItem = {type_name, extensions};

	nfdresult_t dialog_result = NFD_SaveDialog(&outPath, &filterItem, 1, default_path, default_name);

	if (dialog_result == NFD_OKAY) {
		strncpy(last_path, outPath, PATH_MAX);
		NFD_FreePath(outPath);
	}
	else {
		result = NULL;
	}

	NFD_Quit();
	return result;
}
