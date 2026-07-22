/* pocomode.c - poco library functions that get/set drawing state
   and other variables. */

#include <stdio.h>
#include <string.h>

#include "jimk.h"
#include "errcodes.h"
#include "pocoface.h"
#include "pocolib.h"
#include "options.h"
#include "util.h"
#include "render.h"
#include "inks.h"
#include "brush.h"

extern Errcode builtin_err;

extern void set_ccycle(bool newcyc);

/*****************************************************************************
 * ErrCode SetInk(char *name)
 ****************************************************************************/
static Errcode po_ink_set(char* name)
{
	extern Option_tool* ink_list;
	Option_tool* l;

	if (name == NULL) {
		return builtin_err = Err_null_ref;
	}

	l = ink_list;
	while (l != NULL) {
		if (txtcmp(name, l->name) == 0) {
			free_render_cashes();
			set_curink(l);
			set_render_fast();
			make_render_cashes();
			return Success;
		}
		l = l->next;
	}
	return Err_not_found;
}

/*****************************************************************************
 * void GetInk(char *buf)
 ****************************************************************************/
static void po_get_ink(char* name)
{
	if (name == NULL) {
		builtin_err = Err_null_ref;
		return;
	}
	strcpy(name, vl.ink->ot.name);
}

/*****************************************************************************
 * void SetInkStrength(int percent)
 ****************************************************************************/
static void po_ink_strength(int percent)
{
	if (percent > 100) {
		percent = 100;
	}
	if (percent < 0) {
		percent = 0;
	}
	free_render_cashes();
	vl.ink->strength = percent;
	make_render_cashes();
}

/*****************************************************************************
 * int GetInkStrength(void)
 ****************************************************************************/
static int po_get_ink_strength(void)
{
	return vl.ink->strength;
}

/*****************************************************************************
 * void SetInkDither(bool dither)
 ****************************************************************************/
static void po_ink_dither(bool dither)
{
	free_render_cashes();
	vl.ink->dither = dither;
	make_render_cashes();
}

/*****************************************************************************
 * bool GetInkDither(void)
 ****************************************************************************/
static bool po_get_ink_dither(void)
{
	return vl.ink->dither;
}

/*****************************************************************************
 * void SetFilled(bool fill)
 ****************************************************************************/
static void po_tool_fill(bool fill)
{
	vs.fillp = fill;
}

/*****************************************************************************
 * bool GetFilled(void)
 ****************************************************************************/
static bool po_get_fill(void)
{
	return vs.fillp;
}

/*****************************************************************************
 * void SetBrushSize(int size)
 ****************************************************************************/
static void po_tool_brush_size(int size)
{
	vs.use_brush = (size != 0);
	set_circle_brush(size);
}

/*****************************************************************************
 * int GetBrushSize(void)
 ****************************************************************************/
static int po_get_brush_size(void)
{
	return vs.use_brush ? get_brush_size() : 0;
}

/*****************************************************************************
 * void SetKeyMode(bool clear)
 ****************************************************************************/
static void po_set_key_clear(bool clear)
{
	vs.zero_clear = (clear != 0);
}

/*****************************************************************************
 * bool GetKeyMode(void)
 ****************************************************************************/
static bool po_get_key_clear(void)
{
	return vs.zero_clear;
}

/*****************************************************************************
 * void SetKeyColor(int color)
 ****************************************************************************/
static void po_set_key_color(int color)
{
	vs.inks[0] = color & 0xff;
}

/*****************************************************************************
 * int GetKeyColor(void)
 ****************************************************************************/
static int po_get_key_color(void)
{
	return vs.inks[0];
}

/*****************************************************************************
 * void SetMaskUse(bool use_it)
 ****************************************************************************/
static void po_set_mask_use(bool use_it)
{
	vs.use_mask = use_it;
	if (vs.use_mask) {
		vs.make_mask = 0;
	}
	set_render_fast();
	free_render_cashes();
	make_render_cashes();
}

/*****************************************************************************
 * bool GetMaskUse(void)
 ****************************************************************************/
static bool po_get_mask_use(void)
{
	return vs.use_mask;
}

/*****************************************************************************
 * void SetMaskCreate(bool make_it)
 ****************************************************************************/
static void po_set_mask_make(bool make_it)
{
	vs.make_mask = make_it;
	if (vs.make_mask) {
		vs.use_mask = 0;
	}
	set_render_fast();
	free_render_cashes();
	make_render_cashes();
}

/*****************************************************************************
 * bool GetMaskCreate(void)
 ****************************************************************************/
static bool po_get_mask_make(void)
{
	return vs.make_mask;
}

/*****************************************************************************
 * int GetStarPoints(void)
 ****************************************************************************/
static int po_get_star_points(void)
{
	return vs.star_points;
}

/*****************************************************************************
 * void SetStarPoints(int points)
 ****************************************************************************/
static void po_set_star_points(int points)
{
	extern Qslider star_points_sl;
	vs.star_points = clip_to_slider(points, &star_points_sl);
}

/*****************************************************************************
 * int GetStarRatio(void)
 ****************************************************************************/
static int po_get_star_ratio(void)
{
	return vs.star_ratio;
}

/*****************************************************************************
 * void SetStarRatio(int ratio)
 ****************************************************************************/
static void po_set_star_ratio(int ratio)
{
	extern Qslider star_ratio_sl;
	vs.star_ratio = clip_to_slider(ratio, &star_ratio_sl);
}

/*****************************************************************************
 * void GetSplineTCB(int *t, int *c, int *b)
 ****************************************************************************/
static void po_get_spline_tcb(Popot t, Popot c, Popot b)
{
	if (t.pt == NULL || c.pt == NULL || b.pt == NULL) {
		builtin_err = Err_null_ref;
	} else {
		*(int*)t.pt = vs.sp_tens;
		*(int*)c.pt = vs.sp_cont;
		*(int*)b.pt = vs.sp_bias;
	}
}

/*****************************************************************************
 * void SetSplineTCB(int t, int c, int b)
 ****************************************************************************/
static void po_set_spline_tcb(int t, int c, int b)
{
	extern Qslider otens_sl, ocont_sl, obias_sl;

	vs.sp_tens = clip_to_slider(t, &otens_sl);
	vs.sp_cont = clip_to_slider(c, &ocont_sl);
	vs.sp_bias = clip_to_slider(b, &obias_sl);
}

/*****************************************************************************
 * bool GetTwoColorOn(void)
 ****************************************************************************/
static bool po_get_two_color(void)
{
	return vs.color2;
}

/*****************************************************************************
 * void SetTwoColorOn(bool setit)
 ****************************************************************************/
static void po_set_two_color(bool setit)
{
	vs.color2 = setit;
}

/*****************************************************************************
 * int GetTwoColor(void)
 ****************************************************************************/
static int po_get_outline_color(void)
{
	return vs.inks[7];
}

/*****************************************************************************
 * void SetTwoColor(int color)
 ****************************************************************************/
static void po_set_outline_color(int color)
{
	vs.inks[7] = (color & 255);
}

/*****************************************************************************
 * bool GetClosed(void)
 ****************************************************************************/
static bool po_get_poly_closed(void)
{
	return vs.closed_curve;
}

/*****************************************************************************
 * void SetClosed(bool closed)
 ****************************************************************************/
static void po_set_poly_closed(bool closed)
{
	vs.closed_curve = closed;
}

/*****************************************************************************
 * bool GetCycleDraw(void)
 ****************************************************************************/
static bool po_get_cycle_draw(void)
{
	return vs.cycle_draw;
}

/*****************************************************************************
 * void SetCycleDraw(bool cycle)
 ****************************************************************************/
static void po_set_cycle_draw(bool cycle)
{
	set_ccycle(cycle);
}

/*****************************************************************************
 * bool GetMultiFrame(void)
 ****************************************************************************/
static bool po_get_multi(void)
{
	return vs.multi;
}

/*****************************************************************************
 * void SetMultiFrame(bool multi)
 ****************************************************************************/
static void po_set_multi(bool multi)
{
	vs.multi = multi;
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

PolibMode po_libmode = {
	po_ink_set,           "ErrCode SetInk(char *name);",
	po_get_ink,           "void    GetInk(char *buf);",
	po_ink_strength,      "void    SetInkStrength(int percent);",
	po_get_ink_strength,  "int     GetInkStrength(void);",
	po_ink_dither,        "void    SetInkDither(Boolean dither);",
	po_get_ink_dither,    "Boolean GetInkDither(void);",
	po_tool_fill,         "void    SetFilled(Boolean fill);",
	po_get_fill,          "Boolean GetFilled(void);",
	po_tool_brush_size,   "void    SetBrushSize(int size);",
	po_get_brush_size,    "int     GetBrushSize(void);",
	po_set_key_clear,     "void    SetKeyMode(Boolean clear);",
	po_get_key_clear,     "Boolean GetKeyMode(void);",
	po_set_key_color,     "void    SetKeyColor(int color);",
	po_get_key_color,     "int     GetKeyColor(void);",
	po_set_mask_use,      "void    SetMaskUse(Boolean use_it);",
	po_get_mask_use,      "Boolean GetMaskUse(void);",
	po_set_mask_make,     "void    SetMaskCreate(Boolean make_it);",
	po_get_mask_make,     "Boolean GetMaskCreate(void);",
	po_set_star_points,   "void    SetStarPoints(int points);",
	po_get_star_points,   "int     GetStarPoints(void);",
	po_set_star_ratio,    "void    SetStarRatio(int ratio);",
	po_get_star_ratio,    "int     GetStarRatio(void);",
	po_set_spline_tcb,    "void    SetSplineTCB(int t, int c, int b);",
	po_get_spline_tcb,    "void    GetSplineTCB(int *t, int *c, int *b);",
	po_set_two_color,     "void    SetTwoColorOn(Boolean setit);",
	po_get_two_color,     "Boolean GetTwoColorOn(void);",
	po_set_outline_color, "void    SetTwoColor(int color);",
	po_get_outline_color, "int     GetTwoColor(void);",
	po_set_poly_closed,   "void    SetClosed(Boolean closed);",
	po_get_poly_closed,   "Boolean GetClosed(void);",
	po_set_cycle_draw,    "void    SetCycleDraw(Boolean cycle);",
	po_get_cycle_draw,    "Boolean GetCycleDraw(void);",
	po_get_multi,         "Boolean GetMultiFrame(void);",
	po_set_multi,         "void    SetMultiFrame(Boolean multi);",
};

Poco_lib po_mode_lib = {
	NULL,
	"Graphics Modes",
	(Lib_proto*)&po_libmode,
	POLIB_MODE_SIZE,
};
