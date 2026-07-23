/* pocofuns.c - Misc poco library functions */
#include "jimk.h"
#include "errcodes.h"
#include "fli.h"
#include "flicel.h"
#include "pocoface.h"
#include "pocolib.h"
#include "palchunk.h"
#include "textedit.h"
#include "mask.h"

extern Errcode builtin_err;

extern Errcode save_fli(char* name);       // from savefli.c
extern Errcode load_the_pic(char* title);  // from vpaint.c

/* from files.c */
extern Errcode load_path(char* name);
extern Errcode save_path(char* name);
extern Errcode load_polygon(char* name);
extern Errcode save_polygon(char* name);

extern Errcode load_palette(char* title, int fitting);  // from palet2.c

extern Errcode save_titles(char* title);  // from options.c

/* A bunch of load/save file functions */

/*****************************************************************************
 * ErrCode LoadFlic(char *name)
 ****************************************************************************/
static Errcode po_load_fli(char* name)
{
	if (name == NULL) {
		return builtin_err = Err_null_ref;
	}
	return resize_load_fli(name);
}

/*****************************************************************************
 * ErrCode SaveFlic(char *name)
 ****************************************************************************/
static Errcode po_save_fli(char* name)
{
	if (name == NULL) {
		return builtin_err = Err_null_ref;
	}
	return save_fli(name);
}

/*****************************************************************************
 * ErrCode LoadPic(char *name)
 ****************************************************************************/
static Errcode po_load_pic(char* title)
{
	if (title == NULL) {
		return builtin_err = Err_null_ref;
	}
	dirties();
	return load_the_pic(title);
}

/*****************************************************************************
 * ErrCode SavePic(char *name)
 ****************************************************************************/
static Errcode po_save_pic(char* title)
{
	if (title == NULL) {
		return builtin_err = Err_null_ref;
	}
	return save_current_pictype(title, vb.pencel);
}

/*****************************************************************************
 * ErrCode LoadScreenPic(Screen *s, char *name)
 ****************************************************************************/
static Errcode po_load_screen_pic(void* screen, char* title)
{
	Rcel* s;
	if (title == NULL) {
		return builtin_err = Err_null_ref;
	}
	if (screen == NULL) {
		s = vb.pencel;
	} else {
		s = screen;
	}
	dirties();
	return load_any_picture(title, s);
}

/*****************************************************************************
 * ErrCode SaveScreenPic(Screen *s, char *name)
 ****************************************************************************/
static Errcode po_save_screen_pic(void* screen, char* title)
{
	Rcel* s;
	if (title == NULL) {
		return builtin_err = Err_null_ref;
	}
	if (screen == NULL) {
		s = vb.pencel;
	} else {
		s = screen;
	}
	return save_current_pictype(title, s);
}

/*****************************************************************************
 * ErrCode LoadCel(char *name)
 ****************************************************************************/
static Errcode po_load_cel(char* name)
{
	if (name == NULL) {
		return builtin_err = Err_null_ref;
	}
	return load_the_cel(name);
}

/*****************************************************************************
 * ErrCode SaveCel(char *name)
 ****************************************************************************/
static Errcode po_save_cel(char* name)
{
	if (name == NULL) {
		return builtin_err = Err_null_ref;
	}
	return save_the_cel(name);
}

/*****************************************************************************
 * ErrCode LoadPath(char *name)
 ****************************************************************************/
static Errcode po_load_path(char* name)
{
	if (name == NULL) {
		return builtin_err = Err_null_ref;
	}
	return load_path(name);
}

/*****************************************************************************
 * ErrCode SavePath(char *name)
 ****************************************************************************/
static Errcode po_save_path(char* name)
{
	if (name == NULL) {
		return builtin_err = Err_null_ref;
	}
	return save_path(name);
}

/*****************************************************************************
 * ErrCode LoadPoly(char *name)
 ****************************************************************************/
static Errcode po_load_poly(char* name)
{
	if (name == NULL) {
		return builtin_err = Err_null_ref;
	}
	return load_polygon(name);
}

/*****************************************************************************
 * ErrCode SavePoly(char *name)
 ****************************************************************************/
static Errcode po_save_poly(char* name)
{
	if (name == NULL) {
		return builtin_err = Err_null_ref;
	}
	return save_polygon(name);
}

/*****************************************************************************
 * ErrCode LoadColors(char *name)
 ****************************************************************************/
static Errcode po_load_colors(char* name)
{
	if (name == NULL) {
		return builtin_err = Err_null_ref;
	}
	return load_palette(name, 1);
}

/*****************************************************************************
 * ErrCode SaveColors(char *name)
 ****************************************************************************/
static Errcode po_save_colors(char* name)
{
	if (name == NULL) {
		return builtin_err = Err_null_ref;
	}
	return pj_col_save(name, vb.pencel->cmap);
}

/*****************************************************************************
 * ErrCode LoadTitles(char *name)
 ****************************************************************************/
static Errcode po_load_titles(char* name)
{
	if (name == NULL) {
		return builtin_err = Err_null_ref;
	}
	return load_titles(name);
}

/*****************************************************************************
 * ErrCode SaveTitles(char *name)
 ****************************************************************************/
static Errcode po_save_titles(char* name)
{
	if (name == NULL) {
		return builtin_err = Err_null_ref;
	}
	return save_titles(name);
}

/*****************************************************************************
 * ErrCode SaveMask(char *name)
 ****************************************************************************/
static Errcode po_save_mask(char* name)
{
	if (name == NULL) {
		return builtin_err = Err_null_ref;
	}
	return save_the_mask(name);
}

/*****************************************************************************
 * ErrCode LoadMask(char *name)
 ****************************************************************************/
static Errcode po_load_mask(char* name)
{
	if (name == NULL) {
		return builtin_err = Err_null_ref;
	}
	return load_the_mask(name);
}

/*----------------------------------------------------------------------------
 * library protos...
 *
 * Maintenance notes:
 *	To add a function to this library, add the pointer and prototype string
 *	TO THE END of the following list.  Then go to POCOLIB.H and add a
 *	corresponding prototype to the library structure typedef therein which
 *	matches the name of the structure below.  When creating the prototype
 *	in POCOLIB.H, remember that all arguments prototyped below as pointers
 *	must be defined as Popot types in the prototype in pocolib.h; any number
 *	of stars in the Poco proto still equate to a Popot with no stars in the
 *	pocolib.h prototype.
 *
 *	DO NOT ADD NEW PROTOTYPES OTHER THAN AT THE END OF AN EXISTING STRUCTURE!
 *	DO NOT DELETE A PROTOTYPE EVER! (The best you could do would be to point
 *	the function to a no-op service routine).  Breaking these rules will
 *	require the recompilation of every POE module in the world.  These
 *	rules apply only to library functions which are visible to POE modules
 *	(ie, most of them).  If a specific typedef name exists in pocolib.h, the
 *	rules apply.  If the protos are coded as a generic array of Lib_proto
 *	structs without an explicit typedef in pocolib.h, the rules do not apply.
 *--------------------------------------------------------------------------*/

PolibAAFile po_libaafile = {
	po_load_fli,        "ErrCode LoadFlic(char *name);",
	po_save_fli,        "ErrCode SaveFlic(char *name);",
	po_load_pic,        "ErrCode LoadPic(char *name);",
	po_save_pic,        "ErrCode SavePic(char *name);",
	po_load_cel,        "ErrCode LoadCel(char *name);",
	po_save_cel,        "ErrCode SaveCel(char *name);",
	po_load_path,       "ErrCode LoadPath(char *name);",
	po_save_path,       "ErrCode SavePath(char *name);",
	po_load_poly,       "ErrCode LoadPoly(char *name);",
	po_save_poly,       "ErrCode SavePoly(char *name);",
	po_load_colors,     "ErrCode LoadColors(char *name);",
	po_save_colors,     "ErrCode SaveColors(char *name);",
	po_load_titles,     "ErrCode LoadTitles(char *name);",
	po_save_titles,     "ErrCode SaveTitles(char *name);",
	po_load_mask,       "ErrCode LoadMask(char *name);",
	po_save_mask,       "ErrCode SaveMask(char *name);",
	po_save_screen_pic, "ErrCode SaveScreenPic(Screen *s, char *name);",
	po_load_screen_pic, "ErrCode LoadScreenPic(Screen *s, char *name);",
};

Poco_lib po_load_save_lib = {
	NULL,
	"Autodesk Animator File",
	(Lib_proto*)&po_libaafile,
	POLIB_AAFILE_SIZE,
};
