/* pocoblit.c - poco library to blit around things, and functions to
   get various screens */

#include "errcodes.h"
#include "jimk.h"
#include "pocoface.h"
#include "pocolib.h"
#include "flicel.h"
#include "pentools.h"
#include "zoom.h"

extern Poco_lib po_blit_lib;
extern Flicel* thecel;

/*****************************************************************************
 * library resources cleanup routine
 ****************************************************************************/
static void free_allocated_screens(Poco_lib* lib)
{
	Dlheader* sfi = &lib->resources;
	Dlnode *node, *next;

	if (sfi->head == NULL) {
		return;
	}

	for (node = sfi->head; NULL != (next = node->next); node = next) {
		pj_rcel_free(((Rnode*)node)->resource);
		pj_free(node);
	}
}

extern char dirty_frame, dirty_file;

/*****************************************************************************
 * void PicDirtied(void);
 *	allow a poco/poe to indicate that the picscreen has been dirtied.
 ****************************************************************************/
static void po_dirties(void)
{
	dirties();
	save_undo();
	zoom_it();
}

/*****************************************************************************
 * Screen *GetPicScreen(void);
 ****************************************************************************/
void* po_get_screen(void)
{
	return vb.pencel;
}

/*****************************************************************************
 * Screen *GetSwapScreen(void);
 ****************************************************************************/
void* po_get_swap(void)
{
	return vl.alt_cel;
}

/*****************************************************************************
 * Screen *GetUndoScreen(void);
 ****************************************************************************/
void* po_get_undo(void)
{
	return undof;
}

/*****************************************************************************
 * Screen *GetCelScreen(void);
 ****************************************************************************/
static void* po_get_celscreen(void)
{
	if (thecel == NULL) {
		return NULL;
	}
	return thecel->rc;
}

/*****************************************************************************
 * void SetPixel(Screen *s, int color, int x, int y);
 ****************************************************************************/
void po_a_dot(void* pscreen, int color, int x, int y)
{
	Raster* r;
	if ((r = pscreen) == NULL) {
		builtin_err = Err_null_ref;
		return;
	}
	pj_put_dot(r, color, x, y);
	if (r == (Raster*)vb.pencel) {
		dirty_file = dirty_frame = 1;
	}
}

/*****************************************************************************
 * int GetPixel(Screen *s, int x, int y);
 ****************************************************************************/
int po_a_get_dot(void* pscreen, int x, int y)
{
	if (pscreen == NULL) {
		return (builtin_err = Err_null_ref);
	}
	return (pj_get_dot(pscreen, x, y));
}

/*****************************************************************************
 * void GetScreenSize(Screen *s, int *x, int *y);
 ****************************************************************************/
void po_get_dims(void* pscreen, int* width, int* height)
{
	if (pscreen == NULL || width == NULL || height == NULL) {
		builtin_err = Err_null_ref;
		return;
	}
	*width = ((Raster*)pscreen)->width;
	*height = ((Raster*)pscreen)->height;
}

/*****************************************************************************
 * void CopyScreen(Screen *source, Screen *dest);
 ****************************************************************************/
static void po_copy_screen(void* s, void* d)
{
	Rcel *scel, *dcel;
	bool csame;

	if ((scel = s) == NULL || (dcel = d) == NULL) {
		builtin_err = Err_null_ref;
		return;
	}
	if (scel->width != dcel->width || scel->height != dcel->height) {
		builtin_err = Err_wrong_res;
		return;
	}
	csame = cmaps_same(vb.pencel->cmap, undof->cmap);
	pj_rcel_copy(scel, dcel);
	if (dcel == vb.pencel) {
		see_cmap();
		dirties();
	}
}

/*****************************************************************************
 * void TradeScreen(Screen *a, Screen *b);
 ****************************************************************************/
void po_swap_screen(void* s, void* d)
{
	Rcel *scel, *dcel;
	bool csame;

	if ((scel = s) == NULL || (dcel = d) == NULL) {
		builtin_err = Err_null_ref;
		return;
	}
	if (scel->width != dcel->width || scel->height != dcel->height) {
		builtin_err = Err_wrong_res;
		return;
	}
	csame = cmaps_same(vb.pencel->cmap, undof->cmap);
	swap_pencels(scel, dcel);
	if (dcel == vb.pencel) {
		see_cmap();
		dirties();
	}
}

/*****************************************************************************
 * void SetBlock(Screen *s, char *pixbuf, int x, int y, int width, int height);
 ****************************************************************************/
static void po_put_rectpix(void* r, char* pixbuf, int x, int y, int width, int height)
{
	if (width < 0 || height < 0) {
		builtin_err = Err_parameter_range;
		return;
	}
	if (r == NULL || pixbuf == NULL) {
		builtin_err = Err_null_ref;
		return;
	}
	/* We do this one line at a time since put_hseg does clipping but
	 * put_rectpix does not. */
	while (--height >= 0) {
		pj_put_hseg(r, pixbuf, x, y, width);
		pixbuf += width;
		y += 1;
	}
	if (r == (void*)vb.pencel) {
		dirties();
	}
}

/*****************************************************************************
 * void GetBlock(Screen *s, char *pixbuf, int x, int y, int width, int height);
 ****************************************************************************/
static void po_get_rectpix(void* r, char* pixbuf, int x, int y, int width, int height)
{
	if (width < 0 || height < 0) {
		builtin_err = Err_parameter_range;
		return;
	}
	if (r == NULL || pixbuf == NULL) {
		builtin_err = Err_null_ref;
		return;
	}
	pj_get_rectpix(r, pixbuf, x, y, width, height);
}

/*****************************************************************************
 * void IconBlit(char *source, int snext, int sx, int sy, int width, int height
 ****************************************************************************/
static void po_icon_blit(void* msource, int mbpr, int mx, int my, int width, int height, void* dest,
						 int dx, int dy, int color)
{
	if (msource == NULL || dest == NULL) {
		builtin_err = Err_null_ref;
		return;
	}
	if (width < 0 || height < 0) {
		builtin_err = Err_parameter_range;
		return;
	}
	color &= 0xff;
	pj_mask1blit(msource, mbpr, mx, my, dest, dx, dy, width, height, color);
	if (dest == (void*)vb.pencel) {
		dirties();
	}
}

/*****************************************************************************
 * void Blit(Screen *source, int sx, int sy, int width, int height
 ****************************************************************************/
static void po_blit(void* source, int sx, int sy, int width, int height, void* dest, int dx, int dy)
{
	if (source == NULL || dest == NULL) {
		builtin_err = Err_null_ref;
		return;
	}
	if (width < 0 || height < 0) {
		builtin_err = Err_parameter_range;
		return;
	}
	pj_blitrect(source, sx, sy, dest, dx, dy, width, height);
	if (dest == (void*)vb.pencel) {
		dirties();
	}
}

/*****************************************************************************
 * void KeyBlit(Screen *source, int sx, int sy, int width, int height
 ****************************************************************************/
static void po_key_blit(void* source, int sx, int sy, int width, int height, void* dest, int dx,
						int dy, int key_color)
{
	if (source == NULL || dest == NULL) {
		builtin_err = Err_null_ref;
		return;
	}
	if (width < 0 || height < 0) {
		builtin_err = Err_parameter_range;
		return;
	}
	pj_tblitrect(source, sx, sy, dest, dx, dy, width, height, key_color);
	if (dest == (void*)vb.pencel) {
		dirties();
	}
}

/*****************************************************************************
 * ErrCode AllocScreen(Screen **screen, int width, int height);
 ****************************************************************************/
static Errcode po_alloc_screen(Popot* p, int w, int h)
{
	Errcode err;
	Rnode* r;

	if (p == NULL) {
		return (builtin_err = Err_null_ref);
	}
	if (NULL == (r = pj_zalloc(sizeof(Rnode)))) {
		return (builtin_err = Err_no_memory);
	}

	if (w < 0 || h < 0) {
		pj_free(r);
		return Err_parameter_range;
	}

	p->min = p->max = NULL;

	if ((err = valloc_anycel(&p->pt, w, h)) < Success) {
		pj_free(r);
		return (err);
	}

	add_head(&po_blit_lib.resources, &r->node);
	r->resource = p->pt;
	return (Success);
}

/*****************************************************************************
 * void FreeScreen(Screen **screen);
 ****************************************************************************/
static void po_free_screen(Popot* p)
{
	Rnode* r;

	if (p == NULL) {
		builtin_err = Err_null_ref;
		return;
	}

	if (NULL == (r = po_in_rlist(&po_blit_lib.resources, p->pt))) {
		builtin_err = Err_free_resources;
		return;
	}
	rem_node((Dlnode*)r);
	pj_free(r);

	pj_rcel_free(p->pt);
	p->pt = NULL;
}

/*****************************************************************************
 * Screen *GetPhysicalScreen(void);
 ****************************************************************************/
void* po_get_physical_screen(void)
{
	return vb.screen;
}

/*****************************************************************************
 *  void    SetBox(Screen *s, int color, int x, int y, int width, int height);
 ****************************************************************************/
void po_set_box(void* s, int color, int x, int y, int width, int height)
{
	if (s == NULL) {
		builtin_err = Err_null_ref;
		return;
	}
	pj_set_rect(s, color, x, y, width, height);
	if (s == (void*)vb.pencel) {
		dirties();
	}
}

/*****************************************************************************
 * void	MenuText(Screen *screen, int color, int xoff, int yoff, char *text);
 * 		Draw text on any screen in font used for menus.
 ****************************************************************************/
void po_menu_text(void* s, int color, int xoff, int yoff, char* text)
{
	if (s == NULL || text == NULL) {
		builtin_err = Err_null_ref;
		return;
	}
	gftext(s, vb.screen->mufont, text, xoff, yoff, color, TM_MASK1, 0);
	if (s == (void*)vb.pencel) {
		dirties();
	}
}

/*****************************************************************************
 * int	MenuTextWidth(char *text);
 * 		Find out width of text string in menu font.
 ****************************************************************************/
int po_menu_text_width(char* text)
{
	if (text == NULL) {
		return (builtin_err = Err_null_ref);
	}
	return (fstring_width(vb.screen->mufont, text));
}

/*****************************************************************************
 * int	MenuTextHeight(void);
 * 		Get height of menu font.
 ****************************************************************************/
int po_menu_text_height(void)
{
	return (tallest_char(vb.screen->mufont));
}

/*****************************************************************************
 * void	GetMenuColors(int *black, int *grey, int *light, int *bright, int *red)
 *		Return the colors commonly used for the menus.
 ****************************************************************************/
static void po_get_menu_colors(int* black, int* grey, int* light, int* bright, int* red)
{
	Pixel* colors = vb.screen->mc_colors;

	if (black == NULL || grey == NULL || light == NULL || bright == NULL || red == NULL) {
		builtin_err = Err_null_ref;
		return;
	}
	*black = colors[MC_BLACK];
	*grey = colors[MC_GREY];
	*light = colors[MC_WHITE];
	*bright = colors[MC_BRIGHT];
	*red = colors[MC_RED];
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

PolibScreen po_libscreen = {
	po_get_screen,
	"Screen  *GetPicScreen(void);",
	po_get_swap,
	"Screen  *GetSwapScreen(void);",
	po_get_undo,
	"Screen  *GetUndoScreen(void);",
	po_get_celscreen,
	"Screen  *GetCelScreen(void);",
	po_alloc_screen,
	"ErrCode AllocScreen(Screen **screen, int width, int height);",
	po_free_screen,
	"void    FreeScreen(Screen **screen);",
	po_get_dims,
	"void    GetScreenSize(Screen *s, int *x, int *y);",
	po_a_dot,
	"void    SetPixel(Screen *s, int color, int x, int y);",
	po_a_get_dot,
	"int     GetPixel(Screen *s, int x, int y);",
	po_put_rectpix,
	"void    SetBlock(Screen *s, char *pixbuf, int x, int y, int width, int height);",
	po_get_rectpix,
	"void    GetBlock(Screen *s, char *pixbuf, int x, int y, int width, int height);",
	po_icon_blit,
	"void    IconBlit(char *source, int snext, int sx, int sy, int width, int height, "
	"Screen *dest, int dx, int dy, int color);",
	po_blit,
	"void    Blit(Screen *source, int sx, int sy, int width, int height, "
	"Screen *dest, int dx, int dy);",
	po_key_blit,
	"void    KeyBlit(Screen *source, int sx, int sy, int width, int height, "
	"Screen *dest, int dx, int dy, int key_color);",
	po_copy_screen,
	"void    CopyScreen(Screen *source, Screen *dest);",
	po_swap_screen,
	"void    TradeScreen(Screen *a, Screen *b);",
	po_dirties,
	"void    PicDirtied(void);",
	/* New with Ani Pro 1.5 */
	po_get_physical_screen,
	"Screen  *GetPhysicalScreen(void);",
	po_set_box,
	"void    SetBox(Screen *s, int color, int x, int y, int width, int height);",
	po_menu_text,
	"void	MenuText(Screen *screen, int color, int xoff, int yoff, char *text);",
	po_menu_text_width,
	"int	MenuTextWidth(char *text);",
	po_menu_text_height,
	"int	MenuTextHeight(void);",
	po_get_menu_colors,
	"void	GetMenuColors(int *black, int *grey, int *light, int *bright, int *red);",
};

Poco_lib po_blit_lib = {
	NULL, "Screen", (Lib_proto*)&po_libscreen, POLIB_SCREEN_SIZE, NULL, free_allocated_screens,
};
