#define GLOBALS_C

/* Globals.c - all the really pervasive global variables. */

#include <stdio.h>

#include "brush.h"
#include "celmenu.h"
#include "composit.h"
#include "errcodes.h"
#include "filemenu.h"
#include "flx.h"
#include "inks.h"
#include "jimk.h"
#include "mask.h"
#include "options.h"
#include "pentools.h"
#include "timemenu.h"

/* The Vsettings structure holds all of Vpaint's state info, all
   that's a fixed size and not too big at least.  The default_vs
   is where we start 1st time program's run.  */

Vsettings default_vs = {
	.id = {sizeof(Vsettings), VSET_VS_ID, VSET_VS_VERS}, /* id for settings */
	.frame_ix = 0,                                       /* frame_ix - frame index */
	.ccolor = 250,                                       /* ccolor - pen color */
	.zoomscale = 2,                                      /* zoomscale */
	.zoom_open = false,                                  /* zoom_open */
	.use_brush = false,                                  /* use_brush */
	.dcoor = true,                                       /* dcoor */

	.fillp = true,             /* fillp  - fill polygons? */
	.color2 = false,           /* color2 - 2 color polygons? */
	.closed_curve = true,      /* closed_curve */
	.multi = false,            /* multi */
	.clear_moveout = true,     /* clear_moveout - (move tool clears old area) */
	.zero_clear = true,        /* zero_clear - color zero transparent in cel */
	.render_under = false,     /* render_under */
	.render_one_color = false, /* render_one_color */
	.fit_colors = true,        /* fit_colors */
	.make_mask = false,        /* make_mask */
	.use_mask = false,         /* use_mask */
	.pal_fit = true,           /* pal_fit */
	.file_type = FTP_FLIC,     /* file_type */
	.inks = {0, 138, 192, 131, 248, 249, 250, 247}, /* inks wells */

	.ink_id = opq_INKID, /* initial selected ink */
	.ink_slots = {opq_INKID, vsp_INKID, tsp_INKID, rvl_INKID, soft_INKID, celt_INKID, anti_INKID,
				  jmb_INKID}, /* initial inks */

	.ptool_id = DRAW_PTOOL,               /* ptool_id - which drawing tool is active */
	.tool_slots = {DRAW_PTOOL, BOX_PTOOL, /* initial pen tools */
				   POLYF_PTOOL, TEXT_PTOOL, SPRAY_PTOOL, FILL_PTOOL, LINE_PTOOL, MOVE_PTOOL},

	.flicentx = 0,
	.flicenty = 0, /* flicentx, flicenty */
	.quickcentx = 0,
	.quickcenty = VS_MAXCOOR, /* quickcentx, quickcenty */
	.zcentx = VS_MAXCOOR / 2,
	.zcenty = VS_MAXCOOR / 2, /* zcentx, zcenty */
	.zwincentx = VS_MAXCOOR / 4 * 3,
	.zwincenty = (VS_MAXCOOR / 20) * 6, /* zwincentx zwincenty */
	.zwinw = VS_MAXCOOR / 2,
	.zwinh = VS_MAXCOOR / 2,    /* zwin width zwin height */
	.tint_percent = 50,         /* tint percent */
	.twin = {220, 140, 50, 30}, /* text window rectangle */
	.text_yoff = 0,
	.tcursor_p = 0, /* text yoffset and text cursor position */
	.top_tool = 0,
	.top_ink = 0, /* top_tool, top_ink - initial scroller_tops */
	.star_points = 6,
	.star_ratio = 33, /* star points , star ratio */
	.gridx = 0,
	.gridy = 0,
	.gridw = VS_MAXCOOR / (320 / 8),
	.gridh = VS_MAXCOOR / (200 / 8), /* grid x y w h */
	.use_grid = false,               /* use_grid */
	.dthresh = 50,                   /* dither threshold */
	.air_speed = 60,
	.air_spread = 32, /* air_speed, air_spread */
	.qdx = 4,
	.qdy = 4, /* quantization x and y */
	.rgcx = VS_MAXCOOR / 2,
	.rgcy = VS_MAXCOOR / 2,                  /* radial gradient center */
	.rgr = (SHORT)((FLOAT)VS_MAXCOOR / 5.5), /* radial gradient radious rel (h+w)*2 */
	.mkx = VS_MAXCOOR / 2,
	.mky = VS_MAXCOOR / 2,   /* marked point mkx mky */
	.transition_frames = 16, /* transition frames */
	.start_seg = 0,
	.stop_seg = 9,         /* start and stop of time segment */
	.browse_action = 0,    /* browse_action (0 = load) */
	.sep_rgb = false,      /* sep_rgb - by rgb if 1, by color ix if 0 */
	.sep_threshold = 10,   /* sep_threshold */
	.ado_tween = true,     /* ado tween */
	.ado_ease = false,     /* ado ease */
	.ado_ease_out = false, /* ease out */
	.ado_pong = false,     /* ado pong */
	.ado_reverse = false,  /* ado reverse */
	.ado_complete = true,  /* ado complete */
	.ado_source = 0,       /* ado source */
	.ado_outline = false,  /* ado outline */

	.ado_mode = 0,    /* ado_mode == spin */
	.ado_spin = 2,    /* ado_spin == turns */
	.ado_size = 3,    /* ado_size == both */
	.ado_path = 0,    /* ado_path == spline */
	.ado_mouse = 0,   /* ado_mouse == xy */
	.ado_szmouse = 0, /* ado_szmouse == proportional */
	.ado_turn = 360,  /* ado_turn == 360 degrees */

#define WIDTH 320
#define HEIGHT 200
	.move3 =
		{
			/* move3 - top of the optics motion stack */
			NULL,
			{WIDTH / 2, HEIGHT / 2, 0},
			{0, 0, 100},
			{0, 0, 0},
			0,
			0,
			{WIDTH / 2, HEIGHT / 2, 0},
			100,
			100,
			100,
			100,
			100,
			100,
			{0, 0, 0},
		},
#undef WIDTH
#undef HEIGHT

	.sp_tens = 0,   /* sp_tens */
	.sp_cont = 0,   /* sp_cont */
	.sp_bias = 0,   /* sp_bias */
	.time_mode = 2, /* time_mode == 2 == to all */

	.sep_box = 0,               /* sep box */
	.marks = {0, 9, 99, 999},   /* time markers */
	.starttr = {0, 10, 20, 30}, /* start of ranges */
	.stoptr = {9, 19, 29, 39},  /* stop of ranges */
	.bframe_ix = 0,             /* bframe_ix */
	.pal_to = 0,                /* pal to - what portion of palette effected by remap */
	.hls = false,               /* hls or rgb mode */
	.use_bun = 1,               /* use_bun */
	.buns =
		{
			{31, /* bundle1 count */
			 {
				 1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16,
				 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
			 }},
			{
				30, /* bundle2 count */
				{
					92,  93,  94,  95,  96,  97,  98,  99,  100, 101, 102, 103, 104, 105, 106,
					107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121,
				},
			},
		},
	.cclose = 20,        /* cclose - closeness threshold */
	.ctint = 50,         /* ctint - strength of tinting in palette */
	.cdraw_ix = 0,       /* cdraw_ix */
	.cycle_draw = false, /* cycle_draw */
	.tit_just = 0,       /* tit_just */
	.tit_scroll = 0,     /* tit_scroll */
	.tit_move = 0,       /* tit_move */
	.pa_tens = 0,        /* pa_tens */
	.pa_cont = 0,        /* pa_cont */
	.pa_bias = 0,        /* pa_bias */
	.pa_closed = 0,      /* pa_closed */
	.cblend = 50,        /* cblend */
	.font_height = 30,   /* font_height */
	.box_bevel = 0,      /* box_bevel */
	.redo = {0, {0}},    /* redo - Empty record */
	.font_type = 0,      /* font_type */
	.ped_yoff = 0,       /* ped_yoff - poco editor cursor position */
	.ped_cursor_p = 0,   /* ped_cursor_p - poco editor window y start */

	.cur_cel_tool = CELPT_MOVE, /* cur_cel_tool */
	.paste_inc_cel = true,      /* paste_inc_cel */
	.cm_blue_last = false,      /* cm_blue_last */
	.cm_move_to_cursor = false, /* cm_move_to_cursor */
	.cm_streamdraw = false,     /* cm_streamdraw */
	.rot_grid = 0,              /* rot_grid */
	.tween_end = 0,             /* tween_end */
	.tween_tool = 0,            /* tween_tool */
	.tween_magnet = 50,         /* tween_magnet */
	.tween_spline = false,      /* tween_spline */

	.pen_brush_type = CIRCLE_BRUSH, /* pen_brush_type */
	.circle_brush_size = 4,         /* circle_brush_size */
	.square_brush_size = 4,         /* square_brush_size */
	.line_brush_size = 4,           /* line_brush_size */
	.line_brush_angle = 0,          /* line_brush_angle */
	.gel_brush_size = 5,            /* gel_brush_size */
	.randseed = 1,                  /* randseed */

	.co_type = COMP_CUT,      /* co_type */
	.co_still = false,        /* co_still */
	.co_cfit = FIT_TOA,       /*  co_cfit */
	.co_reverse = false,      /* co_reverse */
	.co_matchsize = true,     /* co_matchsize */
	.co_b_first = false,      /* co_b_first */
	.co_olap_frames = 0,      /* co_olap_frames */
	.co_venetian_height = 20, /* co_venetian_height */
	.co_louver_width = 20,    /* co_louver_width */
	.co_boxil_width = 20,     /* co_boxil_width */
	.co_boxil_height = 20,    /* co_boxil_height */

	/* expand settings */
	.expand_x = 0, /* expand_x */
	.expand_y = 0, /* expand_y */

	/* font spacing stuff */
	.font_spacing = 0, /* font_spacing */
	.font_leading = 0, /* font_leading */
	.font_unzag = 1,   /* font_unzag */

	/* pic file IO settings */
	.pic_auto_fit_palette = PIC_IO_PAL_FIT, /* on load, don't auto-fit palette; overwrite */
	.pic_write_alpha = PIC_IO_NO_ALPHA,     /* by default, don't write alpha channel */
	.pic_save_quality = 100,                /* on save, save with this quality */
};


Vsettings vs; /* The settings used by program.  Default_vs gets copied
				  here a lot. */

Vbcb vb = /* yep, this is where it is */
	{
		.ivmode = -1,         /* video mode at -1  */
		.init_drawer = "",    /* init_drawer. */
		.vd = NULL,           /* vd */
		.ram_vd = NULL,       /* ram_vd. */
		.cel_a = NULL,        /* cel_a */
		.cel_b = NULL,        /* cel_b. */
		.screen = NULL,       /* screen */
		.scrcent = {0, 0},    /* scrcent */
		.pencel = NULL,       /* pencel. */
		.config_name = NULL,  /* config_name */
		.vdriver_name = NULL, /* vdriver_name */
		.vdriver_mode = 0     /* vdriver_mode. */
};

Vlcb vl; /* local control block data, all zeros, see jimk.h */


Rcel* undof; /* undo cel */

/* Not just any cel, it's THE cel... */
Flicel* thecel;

/* Mr. Mask */
Bitmap* mask_rast; /* the new one !! both are set by alloc mask */


/* Set to 1 when want to blit under something.  Be sure to set it
   back to 0 again when you're through. */
char under_flag;

/* control block for time oriented menus */

Minitime_data flxtime_data = {
	.first_frame = first_frame,           /* first frame */
	.prev_frame = prev_frame,             /* prev */
	.feel_ix = go_time_menu,              /* feel ix */
	.next_frame = next_frame,             /* next */
	.play_it = mplayit,                   /* play it */
	.last_frame = last_frame,             /* last frame */
	.opt_all = go_time_menu,              /* opt_all */
	.opt_tsl_first = qset_first_frame,    /* opt_tsl_first */
	.get_frameix = flx_get_frameix,       /* get_frame_ix */
	.get_framecount = flx_get_framecount, /* get_frame_count */
	.clear_overlays = NULL, /* clear_overlays used to clean up frame before seeking etc */
	.draw_overlays = NULL,  /* draw_overlays used to restore overlays after seeking etc */
	.seek_frame = flx_seek_frame_with_data,
	.olay_stack = 0, /* start with a clear stack */
	.data = NULL,    /* data */
};

/* these bracket scrub_cur_frame() and user flx seeks */
void flx_clear_olays(void)
{
	mini_clear_overlays(&flxtime_data);
}

void flx_draw_olays(void)
{
	mini_draw_overlays(&flxtime_data);
}

bool flx_olays_hidden(void)
{
	return flxtime_data.olay_stack != 0;
}
