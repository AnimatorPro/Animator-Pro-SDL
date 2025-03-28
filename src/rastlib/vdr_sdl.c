/* vdr_sdl.c */

#include <SDL3/SDL.h>
#include <assert.h>
#define VDEV_INTERNALS
#include "errcodes.h"
#include "libdummy.h"
#include "memory.h"
#include "ptrmacro.h"
#include "rastcall.h"
#include "rastlib.h"
#include "vdevcall.h"

// shared space for all the SDL structures and queries
#include "pj_sdl.h"

#define REX_VDRIVER 0x0101U
#define VDEV_VERSION 0

static Errcode sdl_detect(Vdevice *vd);
static Errcode sdl_get_modes(Vdevice *vd, USHORT mode, Vmode_info *pvm);
static char *sdl_mode_text(Vdevice *vd, USHORT mode);

static Errcode sdl_open_graphics(Vdevice *vd, LibRast *r, LONG w, LONG h, USHORT mode);

static Errcode sdl_close_graphics(Vdevice *vd);
static Rastlib *get_sdl_lib(void);

static struct vdevice_lib sdl_device_library = {
	sdl_detect,
	sdl_get_modes,
	sdl_mode_text,
	NOFUNC, /* set_max_height */
	sdl_open_graphics,
	sdl_close_graphics,
	NOFUNC, /* open_cel */
	NOFUNC, /* show_rast */
};

#define MAKE_VMODE_INFO(MODE_IX, WIDTH, HEIGHT)                                                    \
	{                                                                                              \
		sizeof(Vmode_info), MODE_IX, "SDL", 8,                           /* bits */                \
			1,                                                           /* planes */              \
			{WIDTH, WIDTH, WIDTH, 1}, {HEIGHT, HEIGHT, HEIGHT, 1}, true, /* readable */            \
			true,                                                        /* writeable */           \
			true,                                                        /* displayable */         \
			0,                                                           /* fields_per_frame */    \
			1,                                                           /* display_pages */       \
			1,                                                           /* store_pages */         \
			WIDTH *HEIGHT, WIDTH *HEIGHT, true,                          /* palette_vblank_only */ \
			0,  /* screen_swap_vblank_only */                                                      \
			70, /* field_rate */                                                                   \
			0   /* vblank_period */                                                                \
	}

static Vmode_info sdl_infos[] = {
	MAKE_VMODE_INFO(0, 320, 200),
	MAKE_VMODE_INFO(1, 640, 480),
	MAKE_VMODE_INFO(2, 800, 600),
	MAKE_VMODE_INFO(3, 1024, 768),
	MAKE_VMODE_INFO(4, 1280, 800),
	MAKE_VMODE_INFO(5, 1920, 1080),
};

#undef MAKE_VMODE_INFO

static Vdevice sdl_driver = {{REX_VDRIVER, VDEV_VERSION, NULL, NULL, NULL, NULL, NULL}, /* hdr */
							 0,                                       /* first_rtype */
							 1,                                       /* num_rtypes */
							 Array_els(sdl_infos),                    /* mode_count */
							 sizeof(Vdevice_lib) / sizeof(int (*)()), /* dev_lib_count */
							 &sdl_device_library,                     /* lib */
							 NULL,                                    /* grclib */
							 NUM_LIB_CALLS,                           /* rast_lib_count */
							 {0}};

/*--------------------------------------------------------------*/

int pj_get_vmode(void)
{
	return 0;
}

void restore_ivmode(void)
{
}

void pj_wait_vsync(void)
{
}

/*--------------------------------------------------------------*/
/* SDL Vdevice library.                                         */
/*--------------------------------------------------------------*/

static Errcode sdl_detect(Vdevice *vd)
{
	(void)vd;
	return Success;
}

static Errcode sdl_get_modes(Vdevice *vd, USHORT mode, Vmode_info *pvm)
{
	(void)vd;

	if (mode >= Array_els(sdl_infos)) {
		return Err_no_such_mode;
	}

	*pvm = sdl_infos[mode];
	return Success;
}

static char *sdl_mode_text(Vdevice *vd, USHORT mode)
{
	(void)vd;
	(void)mode;

	return "";
}

static void sdl_open_raster(Raster *r, LONG w, LONG h)
{
	static const Rasthdr defaults = {
		RT_MCGA,           /* type */
		8,                 /* effective bit depth */
		NULL,              /* lib */
		6,       5,        /* aspect ratio */
		{0, 0},            /* reserved */
		320,     200, 0, 0 /* w,h,x,y rectangle */
	};
	static const Bmap bm = {
		0,                        /* realmem seg descriptor, filled in at runtime */
		1,                        /* number of bplanes at least 1 */
		Bytemap_bpr(320),         /* rowbytes */
		Byteplane_size(320, 200), /* size of plane (saves code) */
		{NULL},                   /* at least one plane, the pixelated data */
	};

	*((Rasthdr *)r) = defaults;
	r->lib = get_sdl_lib();
	r->width = w;
	r->height = h;

	r->hw.bm = bm;
	r->hw.bm.bpr = w;
	r->hw.bm.psize = w * h;
}

static Errcode sdl_open_graphics(Vdevice *vd, Raster *r, LONG w, LONG h, USHORT mode)
{
	(void)mode;

	const LONG video_scale = pj_sdl_get_display_scale();

//	/* kiki note: on video resize, the app calls open_graphics
//	 * again-- need to make sure this window is dead and all
//	 * other parts of the graphics system are deallocated. */
//	if (window) {
//		SDL_DestroyWindow(window);
//		window = NULL;
//	}

	if (!window) {
		window = SDL_CreateWindow("PJ Paint", w * video_scale, h * video_scale, 0);
		if (!window) {
			SDL_Log("No window: %s\n", SDL_GetError());
			return Err_no_window;
		}

		SDL_SetWindowResizable(window, true);
	}

	renderer = SDL_CreateRenderer(window, NULL);
	if (!renderer) {
		SDL_Log("Could not create renderer: %s", SDL_GetError());
		return Err_no_renderer;
	}

	s_surface = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_INDEX8);
	if (!s_surface) {
		SDL_Log("Failed to allocate main windows surface: %s", SDL_GetError());
		return Err_no_surface;
	}

	render_target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
									  SDL_TEXTUREACCESS_STREAMING, s_surface->w, s_surface->h);
	if (!render_target) {
		SDL_Log("Failed to allocate main render target: %s", SDL_GetError());
		return Err_no_render_target;
	}

	SDL_SetTextureScaleMode(render_target, SDL_SCALEMODE_NEAREST);

	vga_palette = SDL_CreatePalette(256);
	if (!vga_palette) {
		SDL_Log("Failed to allocate main palette: %s", SDL_GetError());
		return Err_no_render_target;
	}

	sdl_open_raster(r, w, h);
	r->hw.bm.bp[0] = s_surface->pixels;
	r->hw.bm.bpr = s_surface->pitch;
	r->type = vd->first_rtype;

	return Success;
}


static Errcode sdl_close_graphics(Vdevice *vd)
{
	(void)vd;

	if (vga_palette) {
		SDL_DestroyPalette(vga_palette);
		vga_palette = NULL;
	}

	if (render_target) {
		SDL_DestroyTexture(render_target);
		render_target = NULL;
	}

	if (s_surface) {
		SDL_DestroySurface(s_surface);
		s_surface = NULL;
	}

	if (renderer) {
		SDL_DestroyRenderer(renderer);
		renderer = NULL;
	}

	return Success;
}


/*--------------------------------------------------------------*/
/* SDL Rastlib.                                                 */
/*--------------------------------------------------------------*/

static Errcode sdl_close_rast(Raster *r)
{
	(void)r;
	return Success;
}

static void sdl_set_colors(Raster *r, LONG start, LONG count, void *cbuf)
{
	(void)r;
	(void)start;

	const uint8_t *cmap = cbuf;
	SDL_Color colors[256];
	int c;

	assert(0 < count && count <= 256);

	for (c = 0; c < count; c++) {
		colors[c].r = cmap[3 * c + 0];
		colors[c].g = cmap[3 * c + 1];
		colors[c].b = cmap[3 * c + 2];
		colors[c].a = SDL_ALPHA_OPAQUE;
	}

	if (!SDL_SetPaletteColors(vga_palette, colors, 0, 256)) {
		SDL_Log("Failed to set palette: %s", SDL_GetError());
	}

	if (!SDL_SetSurfacePalette(s_surface, vga_palette)) {
		fprintf(stderr, "Couldn't set main buffer palette: %s\n", SDL_GetError());
		return;
	}
}

static void sdl_wait_vsync(Raster *r)
{
	(void)r;
	pj_sdl_flip_window_surface();
}

static Rastlib *get_sdl_lib(void)
{
	static Rastlib sdl_lib;
	static bool loaded = false;

	if (!loaded) {
		pj_copy_bytes(pj_get_bytemap_lib(), &sdl_lib, sizeof(sdl_lib));

		sdl_lib.close_raster = sdl_close_rast;

		sdl_lib.set_colors = sdl_set_colors;
		sdl_lib.wait_vsync = sdl_wait_vsync;

		sdl_lib.get_rectpix = NULL; /* don't use bytemap call */
		sdl_lib.put_rectpix = NULL; /* don't use bytemap call */

		pj_set_grc_calls(&sdl_lib);

		loaded = true;
	}

	return &sdl_lib;
}

/*--------------------------------------------------------------*/
/**
 * Function: sdl_open_vdriver
 *
 *  Based on pj_open_mcga_vdriver, pj__vdr_init_open, mcga_get_driver.
 */
static Errcode sdl_open_vdriver(Vdevice **pvd)
{
	Vdevice *vd = &sdl_driver;

	sdl_driver.hdr.init = pj_errdo_success;
	*pvd = vd;

	if (vd->num_rtypes == 0) {
		pj_close_vdriver(pvd);
		return Err_driver_protocol;
	}

	vd->first_rtype = RT_FIRST_VDRIVER;
	vd->grclib = pj_get_grc_lib();

	return Success;
}

/**
 * Function: pj_open_ddriver
 */
Errcode pj_open_ddriver(Vdevice **pvd, char *name)
{
	(void)name;
	return sdl_open_vdriver(pvd);
}
