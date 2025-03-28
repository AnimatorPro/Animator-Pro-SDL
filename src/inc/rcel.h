#ifndef RCEL_H
#define RCEL_H

#ifndef STDTYPES_H
	#include "stdtypes.h"
#endif

#ifndef RECTANG_H
	#include "rectang.h"
#endif

#ifndef RASTCALL_H
	#include "rastcall.h"
#endif

struct cmap;

#include "wndobody.h"

/* a minimal drawing cel designed to be compatible with a window and 
 * raster and window screen but smaller
 * and less complicated for use as a memory rendering area and image 
 * processing buffer.  It is a raster with a few extra fields added 
 * that are common to a window and window screen.
 * It would be a lot easier to maintain things if windows and cels
 * and screens used pointers to rasters but in the interest of speed
 * the raster and it's library are full fields. and common fields 
 * are maintained in common positions.  In this way all the items
 * can be used in with the raster library protocall and be processed
 * with the same rendering routines */

#define CEL_FIELDS \
	struct cmap *cmap

typedef struct rcel {
	RASTHDR_FIELDS;  /* the raster for raster library */

	union {
		Rastbody hw;
		char pad[sizeof(struct wndobody)];
	} u;

	CEL_FIELDS;
} Rcel;

/* items found in gfxlib.lib on rex side */

void pj_rcel_close(Rcel *rc);
void pj_rcel_free(Rcel *c);

Errcode pj_rcel_bytemap_open(Rasthdr *spec,Rcel *cel,LONG num_colors);
Errcode pj_rcel_bytemap_alloc(Rasthdr *spec,Rcel **pcel,LONG num_colors);


/* do not use close_rcel() or free_rcel() on results of make_virtual_rcel() 
 * no cleanup necessary */

bool pj_rcel_make_virtual(Rcel *rc, Rcel *root, Rectangle *toclip);

#ifndef REXLIB_CODE /* host side only */ 

#ifndef VDEVICE_H
	#include "vdevice.h"
#endif

Rcel *center_virtual_rcel(Rcel *root, Rcel *virt, int width, int height);

Errcode open_display_rcel(Vdevice *vd, Rcel *cel,
						  USHORT width, USHORT height, SHORT mode);

Errcode alloc_display_rcel(Vdevice *vd, Rcel **pcel,
						   USHORT width, USHORT height, SHORT mode);

Errcode open_vd_rcel(Vdevice *vd, Rasthdr *spec, Rcel *cel,
					 LONG num_colors, UBYTE displayable);

Errcode alloc_vd_rcel(Vdevice *vd, Rasthdr *spec, Rcel **pcel,
					  LONG num_colors, UBYTE displayable);

/* REXLIB_CODE */ #endif

#ifdef PRIVATE_CODE

/****** rcel stuff in pj but not in graphics lib *******/

#ifndef PROCBLIT_H
	#include "procblit.h"
#endif

Errcode valloc_bytemap(Raster **r, SHORT w, SHORT h);
bool need_fit_cel(Rcel *c);
void cfit_rcel(Rcel *c, struct cmap *dcmap);
void refit_rcel(Rcel *c, struct cmap *ncmap, struct cmap *ocmap);
Rcel *clone_rcel(Rcel *s);
Rcel *clone_any_rcel(Rcel *in);
void pj_rcel_copy(Rcel *s, Rcel *d);
Errcode valloc_ramcel(Rcel **pcel,SHORT w,USHORT h);
Errcode valloc_anycel(Rcel **pcel,SHORT w,USHORT h);
void set_one_val(Rcel *rc, UBYTE clearc, UBYTE destc);
void show_cel_a_sec(Rcel *cel);
Errcode move_rcel(Rcel *rc, bool fit_cel, bool one_color);
void zoom_cel(Rcel *c);
Errcode clip_celrect(Rcel *src, Rectangle *rect, Rcel **clip);

typedef void (*Celblit)
	(Rcel *rcel, SHORT x, SHORT y, Rcel *d, SHORT dx, SHORT dy,
	 SHORT w, SHORT h, Tcolxldat *txd);

/* returns blit for current settings */
extern Celblit get_celblit(bool cfit);
/* returns "move" blit for settings */
extern Celblit get_celmove(bool cfit);
/* returns line processor */
extern Procline get_celprocline(bool cfit);

/* structure and functions for temporarily saving an rcel */
typedef struct rcel_save 
	{
	enum {SSC_NONE = 0,SSC_CEL,SSC_FILE} where;
	Rcel *saved_cel;
	char saved_fname[16];
	} Rcel_save;

/* save cel and record where in rcel_save structure */
Errcode temp_save_rcel(Rcel_save *sc, Rcel *cel);
/* same as above, report error */
Errcode report_temp_save_rcel(Rcel_save *sc, Rcel *cel);
/* restore cel from rcel_save structure */
Errcode temp_restore_rcel(Rcel_save *sc, Rcel *cel);
/* same as above, report error */
Errcode report_temp_restore_rcel(Rcel_save *sc, Rcel *cel);

/* PRIVATE_CODE */ #endif

#endif /* RCEL_H */
