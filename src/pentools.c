/* pentools.c - routines that receive input when the cursor is over the
   drawing screen (and not in a sub-menu).  */

#include "pentools.h"

#include "broadcas.h"
#include "brush.h"
#include "errcodes.h"
#include "fli.h"
#include "inkdot.h"
#include "jimk.h"
#include "marqi.h"
#include "options.h"
#include "palmenu.h"
#include "rastcurs.h"
#include "render.h"
#include "util.h"
#include "zoom.h"


static void save_thik_line_undo(SHORT y1, SHORT y2, SHORT brushsize);
static Errcode dtool(int mode);

static void hide_brush(void)
{
	vl.hide_brush = 1;
}

static void show_brush(void)
{
	vl.hide_brush = 0;
}

static int pticheck(void *dat)
{
	Wndo *ckwndo;
	(void)dat;

	check_top_wndo_pos(); /* move top window if necessary. */
	if (JSTHIT(KEYHIT | MBRIGHT)) {
		return 1;
	}

	if (icb.mset.wndoid == ZOOM_WNDOID) {
		if (!(curs_in_zoombox())) {
			return 1;
		}
		ckwndo = vl.zoomwndo;
	} else {
		ckwndo = (Wndo *)(vb.pencel);
	}

	if (!wndo_dot_visible(ckwndo, icb.sx - ckwndo->behind.x, icb.sy - ckwndo->behind.y)) {
		return 1;
	}

	return 0;
}

/* pen tool initial input  -  basically checks if you've clicked over
   another window or over current drawing window */
bool tti_input(void)
{
	int ret;

	if (!JSTHIT(MBPEN)) {
		display_cursor();
		ret = anim_wait_input(MBPEN, ANY_INPUT, -1, pticheck, NULL);
		undisplay_cursor();
		if (ret) {
			reuse_input();
			return false;
		}
		return true;
	}
	return !pticheck(NULL);
}

/* pen tool initial input  -  basically checks if you've clicked over
   another window or over current drawing window and sets dirties flag if
   you did */
bool pti_input(void)
{
	int ret = tti_input();

	if (ret != false) {
		dirties();
	}

	return ret;
}

/* do it function for a pentool window */
int do_pen_tool(void *wndo)
{
	Wndo *w = wndo;
	Errcode err;

	if (vl.ptool) {
		if (((err = (*(vl.ptool->doptool))(vl.ptool, w)) < Success) && err != Err_abort) {
			softerr(err, "!%s", "tool_fail", vl.ptool->ot.name);
			clear_redo();
		}
	}
	return 1;
}

/* usually from a pulldown menu replaces current pen tool, waits for a
 * pen down then executes pen tool until it exits. Restores
 * the original pen tool on return.  Reports errors from tool */
void do_pentool_once(Pentool *ptool)
{
	Pentool *optool;
	Wndo *w;

	optool = vl.ptool;
	if (set_curptool(ptool) < Success) {
		goto done;
	}
	w = wait_wndo_input(ANY_CLICK);
	if (!pticheck(NULL)) {
		do_pen_tool(w);
	}

done:
	set_curptool(optool);
}

Errcode box_tool(Pentool *pt, Wndo *w)
{
	Errcode err;
	Rectangle rect;
	(void)pt;
	(void)w;

	if (!pti_input()) {
		return Success;
	}
	if ((err = get_rub_rect(&rect)) < 0) {
		goto error;
	}
	save_undo();
	err = save_redo_box(&rect);

error:
	return err;
}

Errcode fill_tool(Pentool *pt, Wndo *w)
{
	Short_xy fpt;
	(void)pt;
	(void)w;

	if (!pti_input()) {
		return Success;
	}

	fpt.x = icb.mx;
	fpt.y = icb.my;
	return save_redo_fill(&fpt);
}

Errcode flood_tool(Pentool *pt, Wndo *w)
{
	Errcode err;
	Short_xy fpt[2];
	(void)pt;
	(void)w;

	err = Success;
	if (!pti_input()) {
		return err;
	}

	fpt[0].x = icb.mx;
	fpt[0].y = icb.my;
	vl.ptool->cursor = &fill_cursor.hdr;
	wait_click();

	if (JSTHIT(MBPEN)) {
		fpt[1].x = icb.mx;
		fpt[1].y = icb.my;
		err = save_redo_flood(fpt);
	}

	vl.ptool->cursor = &pick_cursor.hdr;

	return err;
}

Errcode edge_tool(Pentool *pt, Wndo *w)
{
	Short_xy fpt;
	(void)pt;
	(void)w;

	if (!pti_input()) {
		return Success;
	}
	fpt.x = icb.mx;
	fpt.y = icb.my;
	return save_redo_edge(&fpt);
}

Errcode drizl_tool(Pentool *pt, Wndo *w)
{
	Errcode err;
	(void)pt;
	(void)w;

	disable_lsp_ink();
	err = dtool(DT_DRIZZLE);
	enable_lsp_ink();
	return err;
}

Errcode streak_tool(Pentool *pt, Wndo *w)
{
	(void)pt;
	(void)w;

	return dtool(DT_STREAK);
}

Errcode draw_tool(Pentool *pt, Wndo *w)
{
	Errcode err;
	(void)pt;
	(void)w;

	/* line fill ink would look funky here */
	disable_lsp_ink();
	err = dtool(DT_DRAW);
	enable_lsp_ink();
	return err;
}

/* Get input from mouse/digitizer for draw tool loop */
Errcode dtool_input(Pos_p *p, void *dummy, SHORT mode)
{
	Errcode err;
	(void)dummy;

	switch (mode) {
		case DT_DRAW:
			wait_input(ANY_INPUT);
			break;
		case DT_STREAK:
		case DT_DRIZZLE:
			vsync_wait_input(ANY_INPUT, 1);
			break;
		case DT_GEL:
			check_input(ANY_INPUT);
			break;
		case DT_SPRAY:
			vsync_wait_input(ANY_INPUT, 2);
			break;
	}

	if (!ISDOWN(MBPEN)) {
		return (Success + 1);
	}

	p->x = icb.mx;
	p->y = icb.my;
	p->pressure = icb.pressure;

	err = save_redo_point(p);
	if (err < Success) {
		return err;
	}

	return Success;
}

Errcode dtool_loop(Errcode (*get_posp)(Pos_p *pp, void *idata, SHORT mode), void *idata, SHORT mode)
{
#define DL 3
#define DLMAX 16
#define DLHI (DLMAX * DL)
	SHORT delt, i, j;
	SHORT delts[DL];
	SHORT around;
	SHORT bsize;
	SHORT obsize, pen_width;
	SHORT drx, dry;
	Pos_p rp, op;
	bool first = true;
	Errcode err;

	obsize = get_brush_size();
	pen_width = vs.use_brush ? obsize : 0;

	set_full_gradrect();

	err = make_render_cashes();
	if (err < Success) {
		goto errout;
	}

	err = start_line_undo();
	if (err < Success) {
		goto errout;
	}

	/* deal with initializing speed sampling buffer for drizzle only */
	for (i = 0; i < DL; i++) {
		delts[i] = DLMAX;
	}

	/* some misc counters */
	i = 0;
	around = 0;

	hide_brush();

	for (;;) {
		if ((err = get_posp(&rp, idata, mode)) != Success) {
			break;
		}
		if (first) {
			/* set up drizzle last x/y */
			drx = rp.x;
			dry = rp.y;
			op = rp;
			first = false;
		}
		switch (mode) {
			case DT_STREAK:
				save_undo_brush(rp.y);
				render_brush(rp.x, rp.y);
				break;
			case DT_DRAW:
				save_thik_line_undo(op.y, rp.y, pen_width);
				if ((err = render_line(op.x, op.y, rp.x, rp.y)) < Success) {
					goto draw_error;
				}
				break;
			case DT_DRIZZLE:
				if (around <= 0) /* only do it every 4th time around */
				{
					delt = 0;
					for (j = 0; j < DL; j++) {
						delt += delts[j];
					}

					if (delt >= DLHI) {
						bsize = 0;
					} else {
						bsize = (DLHI / 2 + (DLHI - delt) * pen_width) / (DLHI);
					}

					set_brush_size(bsize);

					delts[i] = intabs(drx - rp.x) + intabs(dry - rp.y);
					drx = rp.x;
					dry = rp.y;
					i++;
					if (i >= DL) {
						i = 0;
					}
					around = 4;
				}
				save_thik_line_undo(op.y, rp.y, bsize);
				if ((err = render_line(op.x, op.y, rp.x, rp.y)) < Success) {
					goto draw_error;
				}
				--around;
				break;
		}
		if (vs.cycle_draw) {
			cycle_ccolor();
		}
		op = rp;
	}

draw_error:
	show_brush();

errout:
	if (vs.cycle_draw) {
		do_color_redraw(NEW_CCOLOR);
	}
	end_line_undo();
	free_render_cashes();
	set_brush_size(obsize);
	return err;

#undef DL
#undef DLMAX
#undef DLHI
}

/* weird tool that makes line thinner the faster you go */
static Errcode dtool(int mode)
{
	Errcode err;

	for (;;) {
		if (!pti_input()) {
			return Success;
		}

		reuse_input(); /* put 1st point back in input stream */
		err = start_save_redo_points();
		if (err >= Success) {
			err = dtool_loop(dtool_input, NULL, mode);
		}
		end_save_redo_points();
		if (err < Success) {
			break;
		}
		save_redo_draw(mode);
	}

	if (err < Success) {
		check_input(ANY_INPUT); /* cause of reuse_input() */
	}

	return err;
}

/************** Start of line-at-a-time undo saver */
static UBYTE *ychanged;

Errcode start_line_undo(void)
{
	pj_cmap_copy(vb.pencel->cmap, undof->cmap);
	if ((ychanged = pj_zalloc(vb.pencel->height)) == NULL) {
		return (Err_no_memory);
	}
	return Success;
}

void end_line_undo(void)
{
	int y;

	if (!ychanged) {
		return;
	}

	y = vb.pencel->height;
	while (--y >= 0) {
		if (!(ychanged[y])) {
			pj_blitrect(vb.pencel, 0, y, undof, 0, y, undof->width, 1);
		}
	}
	pj_freez(&ychanged);
}

void save_line_undo(Coor y)
{
	if (y >= 0 && y < vb.pencel->height) {
		if (!(ychanged[y])) {
			pj_blitrect(vb.pencel, 0, y, undof, 0, y, undof->width, 1);
			ychanged[y] = true;
		}
	}
}

void save_lines_undo(Ucoor start, int count)
{
	while (--count >= 0) {
		save_line_undo(start++);
	}
}

static void save_thik_line_undo(SHORT y1, SHORT y2, SHORT brushsize)
{
	SHORT height;

	height = y2 - y1;
	if (height < 0) {
		height = -height;
		y1 = y2;
	}
	brushsize += 1;
	brushsize >>= 1;
	save_lines_undo(y1 - brushsize, height + (brushsize << 1) + 1);
}

/************** End of line-at-a-time undo saver */
Errcode spray_loop(Errcode (*get_posp)(Pos_p *pp, void *idata, SHORT mode), void *idata,
				   bool redoing)
{
	Errcode err;
	LONG time_max;
	SHORT roff = 0;
	int i;
	Pos_p rp;
	short xy[2];
	short spread;
	bool check_time;
	bool pressure_sensitive = is_pressure();
	Spray_redo sr;

	pj_srandom(1); /* make it repeatable... */
	set_full_gradrect();
	if ((err = make_render_cashes()) < 0) {
		return err;
	}
	if ((err = start_line_undo()) < Success) {
		free_render_cashes();
		return err;
	}
	hide_brush();

	/* get all the speed we can if spraying dots */
	check_time = (!redoing) && vs.use_brush && (get_brush_size() > 2);

	for (;;) {
		if ((err = get_posp(&rp, idata, DT_SPRAY)) != Success) {
			break;
		}

		if (pressure_sensitive) {
			i = ((vs.air_speed * rp.pressure) >> 8);
			if (i < 1) {
				i = 1;
			}
			spread = (vs.air_spread >> 1) + ((vs.air_spread * rp.pressure) >> 9);
			if (spread < 1) {
				spread = 1;
			}
		} else {
			i = vs.air_speed;
			spread = vs.air_spread;
		}

		if (redoing) {
			if (get_spray_redo(&sr)) {
				i = sr.count;
			} else {
				i = 0;
			}
		} else {
			sr.count = i;
			time_max = pj_clock_1000() + 30;
		}

		while (--i >= 0) {
			polar(pj_random() + roff++, pj_random() % spread, xy);
			xy[0] += rp.x;
			xy[1] += rp.y;
			save_undo_brush(xy[1]);
			render_brush(xy[0], xy[1]);
			if (vs.cycle_draw) {
				cycle_ccolor();
			}
			if (check_time && time_max <= pj_clock_1000()) {
				sr.count -= i;
				break;
			}
		}

		if (!redoing) {
			save_spray_redo(&sr);
		}
	}

	show_brush();
	end_line_undo();
	free_render_cashes();
	if (vs.cycle_draw) {
		do_color_redraw(NEW_CCOLOR);
	}

	return err;
}

Errcode spray_tool(Pentool *pt, Wndo *w)
{
	Errcode err;
	(void)pt;
	(void)w;

	for (;;) {
		if (!pti_input()) {
			return Success;
		}
		reuse_input();
		if ((err = start_save_redo_points()) >= Success) {
			err = spray_loop(dtool_input, NULL, false);
			end_save_redo_points();
		}
		save_redo_spray();
		if (err < Success) {
			check_input(ANY_INPUT); /* because of reuse input */
			break;
		}
	}

	return err;
}

Errcode circle_tool(Pentool *pt, Wndo *w)
{
	Errcode err;
	Circle_p circp;
	(void)pt;
	(void)w;

	if (!pti_input()) {
		return Success;
	}
	if ((err = get_rub_circle(&circp.center, &circp.diam, vs.ccolor)) < 0) {
		goto error;
	}
	save_undo();
	save_redo_circle(&circp);

error:
	return err;
}

Errcode line_tool(Pentool *pt, Wndo *w)
{
	Errcode err;
	Short_xy xys[2];
	(void)pt;
	(void)w;

	if (!pti_input()) {
		return Success;
	}
	if ((err = get_rub_line(xys)) < 0) {
		goto error;
	}
	save_undo();
	save_redo_line(xys);

error:
	return err;
}

/* The move tool.  User clips a cel of area after saving it,
   plops cel down in new position, and clears the old position */
static Errcode move_or_copy_tool(bool clear_move_out)
{
	Errcode err;
	Rcel *clipcel;
	Move_p mop;

	if (!pti_input()) {
		return Success;
	}

	hide_mp();

	err = get_rub_rect(&(mop.orig));
	if (err < Success) {
		return err;
	}

	err = clip_celrect(vb.pencel, &mop.orig, &clipcel);
	if (err < Success) {
		goto error;
	}

	save_undo();

	if (move_rcel(clipcel, false, vs.render_one_color) >= 0) {
		mop.new.x = clipcel->x;
		mop.new.y = clipcel->y;
		mop.clear_move_out = clear_move_out;
		save_redo_move(&mop);
	}

error:
	show_mp();
	pj_rcel_free(clipcel);

	return err;
}

Errcode move_tool(Pentool *pt, Wndo *w)
{
	(void)pt;
	(void)w;

	return move_or_copy_tool(true);
}

Errcode copy_tool(Pentool *pt, Wndo *w)
{
	(void)pt;
	(void)w;

	return move_or_copy_tool(false);
}
