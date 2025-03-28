//
// Created by Charles Wardlaw on 2022-10-02.
//

#ifndef ANIMATOR_PRO_PJ_SDL_H
#define ANIMATOR_PRO_PJ_SDL_H

#include <stdtypes.h>


/*--------------------------------------------------------------*/
struct SDL_Surface;
struct SDL_Window;
struct SDL_Renderer;
struct SDL_Rect;

extern struct SDL_Surface*  s_surface;
extern struct SDL_Window*   window;
extern struct SDL_Surface*  s_window_surface;
extern struct SDL_Renderer* renderer;
extern struct SDL_Texture*  render_target;
extern struct SDL_Palette*  vga_palette;


int pj_sdl_get_video_size(LONG* width, LONG* height);
int pj_sdl_get_window_size(LONG* width, LONG* height);
int pj_sdl_get_window_scale(float* x, float* y);
LONG pj_sdl_get_display_scale(void);
struct SDL_FRect pj_sdl_fit_surface(const struct SDL_Surface* source, int target_w, int target_h);
void pj_sdl_flip_window_surface(void);

const char* pj_sdl_resources_path();
const char* pj_sdl_preferences_path();

bool pj_is_directory(const char *path);

void pj_dialog_set_last_path(const char* path);
char* pj_dialog_file_open(const char* type_name, const char* extensions, const char* default_path);
char* pj_dialog_file_save(const char* type_name, const char* extensions,
						  const char* default_path, const char* default_name);

// from sdl_mac.m
const char* pj_sdl_mac_bundle_path();

#ifndef MIN
#define MIN(x,y) (x < y ? x : y)
#endif

#ifndef MAX
#define MAX(x,y) (x > y ? x : y)
#endif

#ifdef _MSC_VER
	#define PATH_MAX 1024
#endif

#endif // ANIMATOR_PRO_PJ_SDL_H
