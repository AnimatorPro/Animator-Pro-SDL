#include "rcel.h"
#include "errcodes.h"
#include "inks.h"
#include "jimk.h"
#include "marqi.h"
#include "memory.h"
#include "rastcall.h"
#include "zoom.h"

/* Find closest colors in this color map to cel's color map, and then
   remap cel's pixels appropriately. */
void cfit_rcel(Rcel* c, Cmap* dcmap)
{
	UBYTE ctable[COLORS];

	fitting_ctable(c->cmap->ctab, dcmap->ctab, ctable);
	xlat_rast(c, ctable, 1);
}

void refit_rcel(Rcel* c, Cmap* ncmap, Cmap* ocmap)
{
	UBYTE cvtab[COLORS];

	nz_fitting_ctable(ocmap->ctab, ncmap->ctab, cvtab);
	xlat_rast(c, cvtab, 1);
}

/* Plop down a cel on screen */
static void see_a_cel(Rcel* cl)
{
	Tcolxldat xld;

	xld.tcolor = vs.inks[0];

	(*(get_celblit(0)))(cl, 0, 0, vb.pencel, cl->x, cl->y, cl->width, cl->height, &xld);
	zoom_cel(cl);
}

/* Erase cel image (presuming undo screen's been saved). */
static void unsee_a_cel(Rcel* c)
{
	zoom_undo_rect(c->x, c->y, c->width, c->height);
}

/* map everything but clearc to destc */
void set_one_val(Rcel* rc, UBYTE clearc, UBYTE destc)
{
	UBYTE table[COLORS];

	pj_stuff_words((destc << 8) + destc, table, COLORS / sizeof(SHORT));
	table[clearc] = clearc;
	xlat_rast(rc, table, 1);
}

void show_cel_a_sec(Rcel* cel)
{
	Marqihdr mh;

	if (cel != NULL) {
		vinit_marqihdr(&mh, 1, 1);
		save_undo();
		see_a_cel(cel);
		mh.smod = 8;
		while (--mh.smod >= 0) {
			mh.dmod = mh.smod;
			marqi_rect(&mh, (Rectangle*)&(cel->RECTSTART));
			wait_a_jiffy(4);
		}
		unsee_a_cel(cel);
	}
}

void zoom_cel(Rcel* c)
{
	rect_zoom_it(c->x, c->y, c->width, c->height);
}

/* this will clip to source bounds, returns Err_clipped if clipped out
 * in which case there is no cel */
Errcode clip_celrect(Rcel* src, Rectangle* rect, Rcel** clip)
{
	Rcel clipcel;

	*clip = NULL;
	if (!pj_rcel_make_virtual(&clipcel, src, rect))
		return (Err_clipped);
	if ((*clip = clone_rcel(&clipcel)) == NULL)
		return (Err_no_memory);
	return 0;
}

/* Move an rcel while minimizing horrible screen flashing.  */
static void delta_move_rcel(Rcel* c, SHORT dx, SHORT dy, Tcolxldat* txl, bool fit_cel)
{
	Celblit blit = get_celmove(fit_cel);
	SHORT ox, oy;

	ox	 = c->x;
	oy	 = c->y;
	c->x = ox + dx;
	c->y = oy + dy;
	do_leftbehind(ox, oy, c->x, c->y, c->width, c->height, (do_leftbehind_func)undo_rect_lbh, NULL);
	(*blit)(c, 0, 0, vb.pencel, c->x, c->y, c->width, c->height, txl);
	if (vs.zoom_open) /* a few nanoseconds here ... */
	{
		do_leftbehind(ox, oy, c->x, c->y, c->width, c->height, (do_leftbehind_func)rect_zoom_it_lbh, NULL);
		zoom_cel(c);
	}
}

/* moves and rcel over the vb.pencel using the undo buffer to refresh,
 * undo must be saved before this is called */
Errcode move_rcel(Rcel* rc, bool fit_cel, bool one_color)
{
	Errcode err;
	SHORT lx, ly, firstx, firsty;
	Tcolxldat xld;
	Pixel fitab[256];
	bool need_remap = fit_cel || one_color;
	Celblit blit	   = get_celblit(need_remap);

	xld.tcolor = vs.inks[0];
	if (fit_cel) {
		fitting_ctable(rc->cmap->ctab, vb.pencel->cmap->ctab, fitab);
		xld.xlat = fitab;
	} else if (one_color) {
		make_one_color_ctable(fitab, xld.tcolor);
		xld.xlat = fitab;
	} else {
		xld.xlat = NULL;
	}
	firstx = rc->x;
	firsty = rc->y;

	(*blit)(rc, 0, 0, vb.pencel, rc->x, rc->y, rc->width, rc->height, &xld);
	zoom_cel(rc);
	if ((err = rub_rect_in_place((Rectangle*)(&rc->RECTSTART))) < 0)
		goto out;

	for (;;) {
		box_coors(rc->x, rc->y, firstx, firsty);
		lx = icb.mx;
		ly = icb.my;
		wait_input(MMOVE | ANY_CLICK);

		if (JSTHIT(MMOVE))
			delta_move_rcel(rc, icb.mx - lx, icb.my - ly, &xld, need_remap);

		if (JSTHIT(ANY_CLICK))
			break;
	}

	if (JSTHIT(KEYHIT | MBRIGHT))
		err = Err_abort;
	else
		err = 0;

out:
	cleanup_toptext();
	unsee_a_cel(rc);
	if (err) {
		rc->x = firstx;
		rc->y = firsty;
	}
	return (err);
}

/********************************************************/
/* blitfuncs for cel blitting and moving returned by get_celblit and
 * get_celmove() */

/* blits a rectangle fron source to dest */
static void celblit(Rcel* src,
					SHORT sx,
					SHORT sy,
					Rcel* dest,
					SHORT dx,
					SHORT dy,
					SHORT w,
					SHORT h,
					Tcolxldat* xld)
{
	(void)xld;
	pj_blitrect((Raster*)src, sx, sy, (Raster*)dest, dx, dy, w, h);
}

/* xlate blits a rectangle from source to dest */
static void celblitxl(Rcel* src,
					  SHORT sx,
					  SHORT sy,
					  Rcel* dest,
					  SHORT dx,
					  SHORT dy,
					  SHORT w,
					  SHORT h,
					  Tcolxldat* xld)
{
	xlatblit((Rcel*)src, sx, sy, (Rcel*)dest, dx, dy, w, h, xld->xlat);
}

/* "T" blits a rectangle from source to dest */
static void celtblit(Rcel* src,
					 SHORT sx,
					 SHORT sy,
					 Rcel* dest,
					 SHORT dx,
					 SHORT dy,
					 SHORT w,
					 SHORT h,
					 Tcolxldat* xld)
{
	pj_tblitrect((Raster*)src, sx, sy, (Raster*)dest, dx, dy, w, h, xld->tcolor);
}

/* xlate "T" blits a rectangle from source to dest */
static void celtblitxl(Rcel* src,
					   SHORT sx,
					   SHORT sy,
					   Rcel* dest,
					   SHORT dx,
					   SHORT dy,
					   SHORT w,
					   SHORT h,
					   Tcolxldat* xld)
{
	procblit((Raster*)src, sx, sy, (Raster*)dest, dx, dy, w, h, tbli_xlatline, xld);
}

/* "U" blits a rectangle from source to dest */
static void celublit(Rcel* src,
					 SHORT sx,
					 SHORT sy,
					 Rcel* dest,
					 SHORT dx,
					 SHORT dy,
					 SHORT w,
					 SHORT h,
					 Tcolxldat* xld)
{
	ublitrect((Raster*)src, sx, sy, (Raster*)dest, dx, dy, w, h, xld->tcolor);
}

/* xlate "U" blits a rectangle from source to dest */
static void celublitxl(Rcel* src,
					   SHORT sx,
					   SHORT sy,
					   Rcel* dest,
					   SHORT dx,
					   SHORT dy,
					   SHORT w,
					   SHORT h,
					   Tcolxldat* xld)
{
	procblit((Raster*)src, sx, sy, (Raster*)dest, dx, dy, w, h, ubli_xlatline, xld);
}

static void celabtblit(Rcel* src,
					   SHORT sx,
					   SHORT sy,
					   Rcel* dest,
					   SHORT dx,
					   SHORT dy,
					   SHORT w,
					   SHORT h,
					   Tcolxldat* xld)
{
	Raster* src_b = (Raster*)undof;
	abprocblit((Raster*)src, sx, sy, (Raster*)dest, dx, dy, w, h, src_b, dx, dy, pj_tbli_line, xld);
}

static void celabtxlblit(Rcel* src,
						 SHORT sx,
						 SHORT sy,
						 Rcel* dest,
						 SHORT dx,
						 SHORT dy,
						 SHORT w,
						 SHORT h,
						 Tcolxldat* xld)
{
	Raster* src_b = (Raster*)undof;
	abprocblit(
	  (Raster*)src, sx, sy, (Raster*)dest, dx, dy, w, h, src_b, dx, dy, tbli_xlatline, xld);
}

static void celabublit(Rcel* src,
					   SHORT sx,
					   SHORT sy,
					   Rcel* dest,
					   SHORT dx,
					   SHORT dy,
					   SHORT w,
					   SHORT h,
					   Tcolxldat* xld)
{
	Raster* src_b = (Raster*)undof;
	abprocblit((Raster*)src, sx, sy, (Raster*)dest, dx, dy, w, h, src_b, dx, dy, ubli_line, xld);
}

static void celabuxlblit(Rcel* src,
						 SHORT sx,
						 SHORT sy,
						 Rcel* dest,
						 SHORT dx,
						 SHORT dy,
						 SHORT w,
						 SHORT h,
						 Tcolxldat* xld)
{
	Raster* src_b = (Raster*)undof;
	abprocblit(
	  (Raster*)src, sx, sy, (Raster*)dest, dx, dy, w, h, src_b, dx, dy, ubli_xlatline, xld);
}

Celblit get_celmove(bool cfit)
{
	if (vs.render_under) {
		if (cfit)
			return celabuxlblit;
		return celabublit;
	}
	if (vs.zero_clear) {
		if (cfit)
			return celabtxlblit;
		return celabtblit;
	}
	if (cfit)
		return celblitxl;
	return celblit;
}

Celblit get_celblit(bool cfit)
{
	if (vs.render_under) {
		if (cfit)
			return celublitxl;
		return celublit;
	}
	if (vs.zero_clear) {
		if (cfit)
			return celtblitxl;
		return celtblit;
	}
	if (cfit)
		return celblitxl;
	return celblit;
}

Procline get_celprocline(bool cfit)
{
	if (vs.render_under) {
		if (cfit)
			return (ubli_xlatline);
		return (ubli_line);
	}
	if (vs.zero_clear) {
		if (cfit)
			return (tbli_xlatline);
		return (pj_tbli_line);
	}
	return NULL; /* straight blit no processing required (may need cfit) */
}
