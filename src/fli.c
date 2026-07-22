/* Fli.c - this file should probably be broken into a few sections.  It has
   the low level routines to uncompress and play-back a FLI flic.  It also
   has a few simple time-oriented utility routines.  Stuff for stepping
   back and forth a single frame.  */

#include "animinfo.h"
#include "auto.h"
#include "errcodes.h"
#include "flx.h"
#include "jimk.h"
#include "pentools.h"
#include "picdrive.h"
#include "picfile.h"
#include "timemenu.h"
#include "unchunk.h"
#include "zoom.h"

static Errcode load_fli(char* title);

Errcode read_flx_frame(Flxfile* flx, Fli_frame* frame, int ix)
{
	Errcode err;

	if (flx->idx[ix].fsize <= sizeof(Fli_frame)) {
		pj_i_get_empty_rec(frame);
		return Success;
	}

	err = xffreadoset(flx->xf, frame, flx->idx[ix].foff, flx->idx[ix].fsize);
	if (err < Success) {
		return softerr(err, "tflx_read1");
	}

	if (frame->type != FCID_FRAME || frame->size != flx->idx[ix].fsize) {
		return softerr(Err_bad_magic, "tflx_frame");
	}

	return Success;
}

/* got buf unfli - have allocated the buffer ok already.  Read in the
   indicated frame from the FLX file, and then call upstairs to
   uncompress it, adds overlay image if present */
Errcode gb_unfli_flx_frame(Flxfile* flx, Rcel* screen, int ix, int wait, Fli_frame* frame)
{
	Errcode err;

	if ((err = read_flx_frame(flx, frame, ix)) < Success) {
		return err;
	}
	pj_fli_uncomp_frame(screen, frame, wait);
	unfli_flx_overlay(flx, screen, ix);
	return Success;
}

/* seeks to frame from current position by going around through ring frame
 * and not re reading frame 0 returns actual index left in screen */
static Errcode gb_flx_ringseek(Flxfile* flx, Rcel* screen, int curix, int ix, Fli_frame* frame)
{
	Errcode err;

	ix = fli_wrap_frame((Flifile*)flx, ix);
	curix = fli_wrap_frame((Flifile*)flx, curix);

	while (curix != ix) {
		++curix;
		err = gb_unfli_flx_frame(flx, screen, curix, 0, frame);
		if (err < 0) {
			return softerr(err, "tflx_read2");
		}
		if (curix == flix.hdr.frame_count) {
			curix = 0;
		}
	}
	return ix;
}

Errcode gb_unfli(Rcel* screen, /* screen recieving update */
				 int ix,       /* which frame of file to read */
				 int wait,     /* wait for vblank
								* (and update hardware color registers)? */
				 Fli_frame* frame)
{
	return gb_unfli_flx_frame(&flix, screen, ix, wait, frame);
}

Errcode flx_ringseek(Rcel* screen, int curix, int ix)
{
	Errcode err;
	Fli_frame* frame;

	err = pj_fli_cel_alloc_cbuf(&frame, screen);
	if (err < 0) {
		return softerr(err, "tflx_seek");
	}
	err = gb_flx_ringseek(&flix, screen, curix, ix, frame);
	pj_free(frame);
	return err;
}

/* allocate a buffer to read in a compressed delta frame from FLX (indexed
   FLI) file.  If can't allocate buffer go swap out everything we can
   and try again.  Once got the buffer call above routine to read in
   frame and eventually uncompress it. */
Errcode unfli(Rcel* f,  /* screen to update */
			  int ix,   /* which frame of file to read */
			  int wait) /* wait for vblank (and update hardward color registers)? */
{
	struct fli_frame* frame; /* buffer area */
	Errcode err;
	long size;
	int pushed = 0;

	size = flix.idx[ix].fsize;
	frame = pj_malloc(Max(size, sizeof(Fli_frame)));
	if (frame == NULL) {
		pushed = 1;
		push_most();
		frame = begmem(size);
		if (frame == NULL) {
			err = Err_reported;
			goto OUT;
		}
	}
	err = gb_unfli_flx_frame(&flix, f, ix, wait, frame);

OUT:
	pj_gentle_free(frame);
	if (pushed) {
		pop_most();
	}
	return err;
}

Errcode flisize_error(Errcode err, SHORT width, SHORT height)
{
	if (width == vb.pencel->width && height == vb.pencel->height) {
		return softerr(err, "!%d%d", "tflx_buffers", width, height);
	} else {
		return softerr(err, "!%d%d", "fli_big", width, height);
	}
}

/* Make sure that a file-name refers to an existing FLI file
 * or a valid pic format.
 * attempt reset of current fli window environment to
 * the new image size, and attempt to load the images.
 * if a failure in resizing or loading you will end up with an
 * empty fli and an error reported */
Errcode resize_load_fli(char* flicname)
{
	Errcode err;
	Anim_info anim_info;
	char pdr_name[PATH_SIZE];

	hide_mp();

	err = find_pdr_loader(flicname, true, &anim_info, pdr_name, vb.pencel);
	if (err < Success) {
		goto reshow_out;
	}

	unzoom();
	push_most();
	close_temp_flx();

	err = set_penwndo_size((SHORT)anim_info.width, (SHORT)anim_info.height);
	if (err < Success) {
		err = flisize_error(err, (SHORT)anim_info.width, (SHORT)anim_info.height);
		empty_tempflx(1);
		goto error;
	} else {
		if (is_fli_pdr_name(pdr_name)) {
			err = load_fli(flicname);
			goto done;
		}

		if (anim_info.num_frames == 1) {
			empty_tempflx(1);
			err = pdr_load_picture(pdr_name, flicname, vb.pencel);
			if (err < Success) {
				goto error;
			}
			dirties();
			goto done;
		}
		/* try to load animation file using pdr */

		if (!soft_yes_no_box("!%d%s", "fliload_slow", anim_info.num_frames, flicname)) {
			err = Err_abort;
			goto error;
		}

		vs.frame_ix = 0;
		err = make_pdr_tempflx(pdr_name, flicname, &anim_info);
		if (err >= Success || err == Err_abort) {
			err = unfli(vb.pencel, 0, 1);
			if (err >= Success) {
				goto done;
			}
		}
		kill_seq();
		goto error;
	}

done:
error:
	pop_most();
	rezoom();

reshow_out:
	show_mp();
	vs.bframe_ix = 0; /* back frame buffer no good now */
	return softerr(err, "!%s", "fli_load", flicname);
}

/* resets settings to default values in fli (default_name) and creates
 * an empty tflx for these settings at current fli window size */
Errcode open_default_flx(void)
{
	Errcode err;
	Vset_flidef fdef;

	err = load_default_settings(&fdef);
	if (err >= Success) {
		err = empty_tempflx(Max(fdef.frame_count, 1));
		flix.hdr.speed = fdef.speed;
	}
	return err;
}


#ifdef WITH_POCO

/* called from poco and the like to reset to default settings */
Errcode resize_default_temps(SHORT width, SHORT height)
{
	Errcode err, ret;

	ret = Success;
	hide_mp();
	unzoom();
	push_most();

	//! FIXME: where did this go?
	// close_tflx();

	if (width > 0 && height > 0) {
		ret = set_penwndo_size(width, height);
		if (ret < 0) {
			ret = flisize_error(ret, width, height);
		}
	}

	err = open_default_flx();
	if (err < 0) {
		return err;
	}

	pop_most();
	rezoom();
	show_mp();
	return ret;
}
#endif


/* Convert a FLI file into an indexed frame (FLX) file - into our
   main temp file in fact.  Make first frame visible.  On failure
   generate an empty FLX file */
static Errcode load_fli(char* title)
{
	Errcode err;

	vs.frame_ix = 0;
	err = make_tempflx(title, 1);
	if (err < 0) {
		goto OUT;
	}
	err = unfli(vb.pencel, 0, 1);
	if (err < 0) {
		goto OUT;
	}
	save_undo();
OUT:
	if (err < 0) {
		kill_seq();
	}
	zoom_it();
	return err;
}

/* Force a frame index to be inside the FLX. (between 0 and count - 1)  */
int wrap_frame(int frame)
{
	frame = frame % flix.hdr.frame_count;
	if (frame < 0) {
		frame += flix.hdr.frame_count;
	}
	return frame;
}

/* If gone past the end go back to the beginning... */
void check_loop(void)
{
	vs.frame_ix = wrap_frame(vs.frame_ix);
}

/* Move frame counter forward one */
void advance_frame_ix(void)
{
	vs.frame_ix++;
	check_loop();
}

SHORT flx_get_frameix(void* data)
{
	(void)data;
	return vs.frame_ix;
}

SHORT flx_get_framecount(void* data)
{
	(void)data;
	return flix.hdr.frame_count;
}


void last_frame(void* data)
/* Do what it takes to move to last frame of our temp file */
{
	(void)data;

	if (flix.xf == NULL) {
		return;
	}

	flx_clear_olays();
	if (vs.frame_ix == flix.hdr.frame_count - 1) {
		return; /* already there... */
	}
	scrub_cur_frame();
	pj_rcel_copy(vb.pencel, undof);
	fli_tseek(undof, vs.frame_ix, flix.hdr.frame_count - 1);
	pj_rcel_copy(undof, vb.pencel);
	see_cmap();
	zoom_it();
	vs.frame_ix = flix.hdr.frame_count - 1;
	flx_draw_olays();
}

/* scrub the cur-frame, and do a tseek */
void flx_seek_frame(SHORT frame)
{
	if (flix.xf == NULL) {
		return;
	}
	flx_clear_olays();
	frame = wrap_frame(frame);
	if (frame != vs.frame_ix) {
		if (frame != (scrub_cur_frame() - 1)) {
			if (frame == vs.frame_ix + 1) {
				/* optimization for just the next frame */
				pj_cmap_copy(vb.pencel->cmap, undof->cmap);
				fli_tseek(vb.pencel, vs.frame_ix, frame);
				if (!cmaps_same(vb.pencel->cmap, undof->cmap)) {
					see_cmap();
				}
				zoom_it();
				save_undo();
			} else {
				save_undo();
				fli_tseek(undof, vs.frame_ix, frame);
				zoom_unundo();
			}
		} else {
			zoom_unundo();
		}

		vs.frame_ix = frame;
	}
	flx_draw_olays();
}

void flx_seek_frame_with_data(SHORT frame, void* data)
{
	(void)data;
	flx_seek_frame(frame);
}

/* do what it takes to go to previous frame of our temp file.  If go before
   first frame then wrap back to last frame */
void prev_frame(void* data)
{
	(void)data;
	flx_seek_frame(vs.frame_ix - 1);
}

/* Jump to first frame of temp file */
void first_frame(void* data)
{
	(void)data;

	if (flix.xf == NULL) {
		return;
	}

	flx_clear_olays();
	scrub_cur_frame();
	if (unfli(undof, 0, 1) >= 0) {
		vs.frame_ix = 0;
	}
	zoom_unundo();
	flx_draw_olays();
}

/* Jump to next frame of temp file, wrapping back to 1st frame if go past
   end... */
void next_frame(void* data)
{
	int oix;
	int undoix;
	(void)data;

	if (flix.xf == NULL) {
		return;
	}

	flx_clear_olays();
	oix = vs.frame_ix;
	undoix = scrub_cur_frame() - 1;
	++vs.frame_ix;
	check_loop();
	if (undoix != vs.frame_ix) {
		if (unfli(vb.pencel, vs.frame_ix, 1) < 0) {
			vs.frame_ix = oix;
		}
		zoom_it();
		save_undo();
	} else {
		zoom_unundo(); /* undo has next frame left in it by sub_cur_frame() */
	}
	flx_draw_olays();
}

/* Ya, go play dem frames.  Replay temp file */
Errcode vp_playit(LONG frames)
{
	Errcode err;
	ULONG clock;

	flx_clear_olays(); /* undraw cels cursors etc */
	if (flix.xf != NULL) {
		clock = pj_clock_1000();
		hide_mouse();
		for (;;) {
			if (frames == 0) {
				err = Success;
				break;
			}
			--frames;
			clock += flix.hdr.speed;

			err = wait_til(clock);
			if (err != Err_timeout) {
				break;
			}
			if (clock > pj_clock_1000()) {
				/* wrap */
				clock = pj_clock_1000();
			}
			vs.frame_ix++;
			if (vs.frame_ix > flix.hdr.frame_count) {
				vs.frame_ix = 1;
			}
			err = unfli(vb.pencel, vs.frame_ix, 1);
			if (err < 0) {
				break;
			}
			zoom_it();
		}
		show_mouse();
	}
	check_loop();     /* to go frame frame_count to 0 sometimes... */
	flx_draw_olays(); /* undraw cels cursors etc */
	return err;
}

/* Play temp file forever */
void mplayit(void* data)
{
	(void)data;

	hide_mp();
	scrub_cur_frame();
	vp_playit(-1L);
	pj_rcel_copy(vb.pencel, undof);
	show_mp();
}

/* Play frames from start to stop of temp file */
static void pflip_thru(int start, int stop, int wait)
{
	Rcel* tmp;
	long count;

	count = stop - start;
	start = wrap_frame(start);
	if (alloc_pencel(&tmp) < 0) {
		return;
	}
	pj_rcel_copy(vb.pencel, tmp);
	fli_tseek(tmp, vs.frame_ix, start);
	pj_rcel_copy(tmp, vb.pencel);
	see_cmap();
	zoom_it();
	pj_rcel_free(tmp);
	if (check_input(MBRIGHT | KEYHIT)) {
		return;
	}
	vs.frame_ix = start;
	wait *= flix.hdr.speed;
	wait_millis(wait);
	if (vp_playit(count) >= Success) {
		/* don't wait if they aborted */
		wait_millis(wait);
	}
}

/* Flip through the time segment without destroying undo buffer or
   other-wise disturbing the 'paint context'.   Once parameter indicates
   whether we stop after doing one time or just keep going until user
   hits a key */
static void fl_range(int once)
{
	int oix;
	Rcel_save opic;

	flx_clear_olays();
	oix = vs.frame_ix;
	find_seg_range();
	if (report_temp_save_rcel(&opic, vb.pencel) >= Success) {
		maybe_push_most();
		for (;;) {
			pflip_thru(tr_r1, tr_r2, (once ? 2 : 0));
			if (JSTHIT(MBRIGHT | KEYHIT) || once) {
				break;
			}
		}
		maybe_pop_most();
		report_temp_restore_rcel(&opic, vb.pencel);
	}
	zoom_it();
	vs.frame_ix = oix;
	flx_draw_olays();
}

/* flip through time segment once */
void flip_range(void)
{
	fl_range(1);
}

/* flip through time segment until key is pressed */
void loop_range(void)
{
	fl_range(0);
}

/* flip through last five frames */
void flip5(void)
{
	Rcel_save opic;

	flx_clear_olays();
	if (report_temp_save_rcel(&opic, vb.pencel) >= Success) {
		flix.hdr.speed <<= 1;
		maybe_push_most();
		pflip_thru(vs.frame_ix - 4, vs.frame_ix, 2);
		maybe_pop_most();
		flix.hdr.speed >>= 1;
		report_temp_restore_rcel(&opic, vb.pencel);
		zoom_it();
	}
	flx_draw_olays();
}

/* Try to load screen from back-frame-buffer to avoid having to
   seek all the way from the 1st frame.  Back frame buffer is an
   uncompressed full screen pic file that tries to stay about 4 frames
   behind current frame. */
static int get_bscreen(Rcel* screen, int cur_ix, int new_ix)
{
	int bix = vs.bframe_ix;
	if (bix != 0) {
		if (bix <= new_ix) {
			if (bix > cur_ix || cur_ix > new_ix) {
				if (pj_exists(bscreen_name)) {
					if (load_pic(bscreen_name, screen, bix, true) >= 0) {
						return bix;
					}
				}
			}
		} else {
			vs.bframe_ix = 0;
		}
	}
	return cur_ix;
}

/* Update the 'back frame buffer' to a new position. */
static void advance_bscreen(Rcel* screen, int ix, int destix)
{
	if (ix >= vs.bframe_ix + 4 && ix > destix - 4) {
		if (save_pic(bscreen_name, screen, ix, true) < 0) {
			vs.bframe_ix = 0;
		} else {
			vs.bframe_ix = ix;
		}
	}
}

/* move screen from one frame to another frame.  Screen must indeed
   contain the frame 'cur_ix'.  */
Errcode fli_tseek(Rcel* screen, int cur_ix, int new_ix)
{
	Errcode err;
	int i;

	cur_ix = get_bscreen(screen, cur_ix, new_ix);
	if (new_ix == cur_ix) {
		return Success;
	}
	if (new_ix > cur_ix) {
		for (i = cur_ix + 1; i <= new_ix; i++) {
			err = unfli(screen, i, 0);
			if (err < 0) {
				return err;
			}
			advance_bscreen(screen, i, new_ix);
		}
	} else {
		for (i = 0; i <= new_ix; i++) {
			err = unfli(screen, i, 0);
			if (err < 0) {
				return err;
			}
			advance_bscreen(screen, i, new_ix);
		}
	}
	return Success;
}
Errcode gb_fli_tseek(Rcel* screen, int cur_ix, int new_ix, struct fli_frame* fbuf)

/* Move screen to a new frame assuming we've got the decompression buffer
   already... */
{
	int i;
	Errcode err;

	cur_ix = get_bscreen(screen, cur_ix, new_ix);
	if (new_ix == cur_ix) {
		return 0;
	}
	if (new_ix > cur_ix && cur_ix != 0) {
		for (i = cur_ix + 1; i <= new_ix; i++) {
			err = gb_unfli(screen, i, 0, fbuf);
			if (err < 0) {
				return err;
			}
			advance_bscreen(screen, i, new_ix);
		}
	} else {
		for (i = 0; i <= new_ix; i++) {
			err = gb_unfli(screen, i, 0, fbuf);
			if (err < 0) {
				return err;
			}
			advance_bscreen(screen, i, new_ix);
		}
	}
	return 0;
}

/* Force screen to a specific frame of temp file.  This will start from
   first frame of file, so the input frame need not contain anything in
   particular */
Errcode gb_fli_abs_tseek(Rcel* screen, int new_ix, struct fli_frame* fbuf)
{
	Errcode err;

	if (vs.bframe_ix == 0 || vs.bframe_ix > new_ix) {
		err = gb_unfli(screen, 0, 0, fbuf);
		if (err < 0) {
			return err;
		}
	}
	return gb_fli_tseek(screen, 0, new_ix, fbuf);
}

/* Force screen to a specific frame of temp file.  This will start from
   first frame of file, so the input frame need not contain anything in
   particular */
Errcode fli_abs_tseek(Rcel* screen, int new_ix)
{
	Errcode err;

	if (vs.bframe_ix == 0 || vs.bframe_ix > new_ix) {
		err = unfli(screen, 0, 0);
		if (err < 0) {
			return err;
		}
	}
	return fli_tseek(screen, 0, new_ix);
}
