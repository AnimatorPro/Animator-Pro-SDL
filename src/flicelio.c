#include "animinfo.h"
#include "commonst.h"
#include "errcodes.h"
#include "flicel.h"
#include "flipath.h"
#include "jimk.h"
#include "menus.h"
#include "picdrive.h"
#include "picfile.h"
#include "softmenu.h"
#include "unchunk.h"
#include "util.h"
#include "zoom.h"
#include <string.h>


/* (re)creates and writes out a .tmp file for a flicel if cel
 * has a temp name a cel with FCEL_RAMONLY flag set cnat have a temp file
 * saved for it */
Errcode save_fcel_temp(Flicel* fc)
{
	Flifile flif;
	Errcode err;
	Chunk_id fchunk;
	char* path;

	if (!fc || !fc->cpath || NULL == (path = fc->tpath)) {
		return Err_bad_input;
	}

	fc->cpath->fid = fc->flif.hdr.id; /* update cpath id from cel fli header */

	err = pj_fli_create(path, &flif);
	if (err < Success) {
		goto error;
	}

	flif.hdr.width		  = fc->flif.hdr.width;
	flif.hdr.height		  = fc->flif.hdr.height;
	flif.hdr.bits_a_pixel = fc->flif.hdr.bits_a_pixel;
	/* frame_count = 0 */

	fchunk.size = sizeof(Celdata) + fc->cpath->id.size;
	fchunk.type = FCID_PREFIX;

	err = xffwrite(flif.xf, &fchunk, sizeof(fchunk));
	if (err < Success) {
		goto error;
	}

	err = xffwrite(flif.xf, &fc->cd, sizeof(Celdata));
	if (err < Success) {
		goto error;
	}

	err = xffwrite(flif.xf, fc->cpath, fc->cpath->id.size);
	if (err < Success) {
		goto error;
	}

	err = pj_i_flush_head(&flif);
	if (err < Success) {
		goto error;
	}

	pj_fli_close(&flif);
	return Success;

error:
	pj_fli_close(&flif);
	pj_delete(path);
	return err;
}

Errcode create_celfli_start(char* tempname, char* fliname, Flicel** pfcel, Rcel* rc)
{
	Errcode err;
	Flicel* fc;

	err = alloc_fcel(pfcel);
	if (err < Success) {
		return err;
	}

	fc	   = *pfcel;
	fc->rc = rc;
	fc->flags |= FCEL_OWNS_RAST;

	if (tempname) {
		fc->tpath = clone_string(tempname);
		if (fc->tpath == NULL) {
			err = Err_no_memory;
			goto error;
		}
	}

	if (fliname) {
		err = pj_fli_create(fliname, &fc->flif);
		if (err < Success) {
			goto error;
		}
	}

	fc->flif.hdr.width		  = rc->width;
	fc->flif.hdr.height		  = rc->height;
	fc->flif.hdr.bits_a_pixel = 8; /* rc->pdepth; */
	fc->flif.hdr.speed		  = 71;

	/* fudge for aspect ratio */
	fc->flif.hdr.aspect_dx = vb.pencel->aspect_dx;
	fc->flif.hdr.aspect_dy = vb.pencel->aspect_dy;

	set_fcel_center(fc, rc->x + (rc->width >> 1), rc->y + (rc->height >> 1));

	if (fliname) {
		err = alloc_flipath(fliname, &fc->flif, &fc->cpath);
		if (err < Success) {
			goto error;
		}
	}

	return Success;

error:
	if (fc) {
		fc->rc = NULL;
		fc->flags &= ~FCEL_OWNS_RAST;
	}
	free_fcel(pfcel);
	if (fliname) {
		pj_delete(fliname);
	}
	return err;
}

/* Makes a one frame flicel from a pic in an rcel it attaches the rcel to it.
 * A celinfo.tmp file is created and a file that has the cel as a 1 frame fli
 * if the file names are non NULL. If either file name is NULL neither file
 * is created, and the flag FCEL_RAMONLY is set.
 * It takes posession of the rcel and will take care of freeing it in
 * free_fcel() note this is only called with a pointer to thecel for
 * now and does a jdelete(cel_name) on error */
Errcode make1_flicel(char* tempname, char* fliname, Flicel** pfcel, Rcel* rc)
{
	Errcode err;
	Flicel* fc;

	if (tempname == NULL || fliname == NULL) {
		tempname = fliname = NULL;
	}

	err = create_celfli_start(tempname, fliname, pfcel, rc);
	if (err < Success) {
		goto error;
	}

	fc = *pfcel;

	set_flicel_tcolor(fc, vs.inks[0]); /* set to current tcolor */

	if (fliname == NULL) {
		fc->flags |= FCEL_RAMONLY;
		fc->flif.hdr.frame_count = 1;
	} else {
		/* create temp cel fli file records with one frame */
		err = pj_write_one_frame_fli(fliname, &fc->flif, rc);
		if (err < Success) {
			goto error;
		}
		pj_fli_close(&fc->flif);

		/* write an info file for this cel's fli */
		err = save_fcel_temp(fc);
		if (err < Success) {
			goto error;
		}
	}

	return Success;

error:
	fc->rc = NULL;
	free_fcel(pfcel);
	if (tempname) {
		pj_delete(tempname);
		pj_delete(fliname);
	}
	return (err);
}

/* loads a fli file as a newly allocated flicel, if tempname is non-null
 * builds a temp fli from the fli or points a tempname file to the fli
 * if the tempname is NULL it will not build a temp file and will point the
 * ram cel to the fli even if the fli is on a removable device */
Errcode load_fli_fcel(char* flipath, char* tempname, char* celfli_name, Flicel** pfc)
{
	Errcode err;
	Flifile flif;
	Flicel* fc;
	LONG chunksize;
	Chunkparse_data pd;
	bool make_flicopy;
	bool found_celdata;
	char device[DEV_NAME_LEN];

	if ((err = alloc_fcel(pfc)) < Success)
		return (err);
	fc = *pfc;

	if (tempname) {
		if (NULL == (fc->tpath = clone_string(tempname))) {
			err = Err_no_memory;
			goto error;
		}
	}

	clear_struct(&flif);

	if ((err = get_path_device(flipath, device)) < Success)
		goto error;

	/* if allowed make sure cel fli file is not on removable drive */
	make_flicopy = (tempname != NULL && celfli_name != NULL && !pj_is_fixed(device));

	/* attempth to open fli requested as cel */

	if (make_flicopy) {
		err = pj_fli_open(flipath, &flif, XREADONLY);
		if (err < Success)
			goto error;
	} else {
		err = pj_fli_open(flipath, &flif, XREADWRITE_OPEN);
		if (err < Success)
			goto error;

		/* we've got to have a valid update time ! */
		if (flif.hdr.id.update_time == 0) {
			if ((err = pj_i_flush_head(&flif)) < Success)
				goto error;
		}
	}

	found_celdata = false;
	init_chunkparse(&pd, flif.xf, FCID_PREFIX, sizeof(Fli_head), 0, 0);
	while (get_next_chunk(&pd)) {
		if (pd.type == FP_CELDATA) {
			if (pd.fchunk.size == sizeof(Celdata)) {
				/* try to read it */
				pd.error	  = read_parsed_chunk(&pd, &fc->cd, -1);
				found_celdata = true;
			}
			break;
		}
	}

	if (pd.error < Success && pd.error != Err_no_chunk) {
		err = pd.error;
		goto error;
	}

	if (!found_celdata) {
		/* No positioning chunk found. Just put cel in upper left corner */
		fc->cd.cent.x = flif.hdr.width >> 1;
		fc->cd.cent.y = flif.hdr.height >> 1;
	}

	/* load fli dimensions and alloc raster for fli frames */

	fc->flif.hdr.width	= flif.hdr.width;
	fc->flif.hdr.height = flif.hdr.height;
	if ((err = alloc_fcel_raster(fc)) < Success)
		goto error;

	refresh_flicel_pos(fc);

	if (make_flicopy) {
		if ((err = pj_fli_create(cel_fli_name, &fc->flif)) < Success)
			goto error;

		fc->flif.hdr = flif.hdr;

		fc->flif.hdr.frame1_oset = sizeof(Fli_head);
		fc->flif.hdr.frame2_oset = sizeof(Fli_head) + (flif.hdr.frame2_oset - flif.hdr.frame1_oset);

		chunksize = flif.hdr.size - flif.hdr.frame1_oset;

		err = pj_copydata_oset(
		  flif.xf, fc->flif.xf, flif.hdr.frame1_oset, fc->flif.hdr.frame1_oset, chunksize);
		if (err < Success) {
			softerr(err, "first_ok");
			err = 0;
		}

		fc->flif.hdr.size = xfftell(fc->flif.xf);
		if (fc->flif.hdr.size < 0) {
			err = fc->flif.hdr.size;
			goto error;
		}
		/* flush final version of header */

		if ((err = pj_i_flush_head(&fc->flif)) < Success)
			goto error;

		pj_fli_close(&flif); /* close source file */
		flipath = cel_fli_name;
	} else {
		fc->flif = flif;	 /* source file is cel fli */
		clear_struct(&flif); /* so won't close twice */
	}

	/* make path record */

	if ((alloc_flipath(flipath, &fc->flif, &fc->cpath)) < Success)
		goto error;

	/* load image and seek to current frame */

	if ((err = seek_fcel_frame(fc, fc->cd.cur_frame)) < Success)
		goto error;

	pj_fli_close(&fc->flif);
	return (Success);
error:
	pj_fli_close(&flif);
	free_fcel(pfc);
	if (tempname)
		pj_delete(tempname);
	if (celfli_name)
		pj_delete(celfli_name);
	return (err);
}

void close_fcelio(Flicel* fc)
{
	pj_fli_close(&fc->flif);
}

/* for an extant fli cel reopen and verify it's file in read only mode */
Errcode reopen_fcelio(Flicel* fc, enum XReadWriteMode mode)
{
	Errcode err;
	char* path;
	Flifile flif;
	Fli_id oid;

	if (fc->flags & FCEL_RAMONLY)
		return Success;

	if (fc->flif.xf != NULL)
		pj_fli_close(&fc->flif);

	if (!fc->cpath)
		return (Err_bad_input);

	path = fc->cpath->path;
	oid	 = fc->cpath->fid;

	err = pj_fli_open(path, &flif, mode);
	if (err < Success)
		goto error;

	if (memcmp(&oid, &flif.hdr.id, sizeof(Fli_id)) || flif.hdr.width != fc->rc->width ||
		flif.hdr.height != fc->rc->height) {
		err = Err_invalid_id;
		goto error;
	}
	fc->flif = flif;
	return (Success);
error:
	pj_fli_close(&flif);
	err = softerr(err, "!%s", "fcel_reopen", path);
	return (err);
}

/* if cbuf is null it will allocate one! */
Errcode gb_seek_fcel_frame(Flicel* fc, SHORT frame, Fli_frame* cbuf, bool force_read)
{
	int i;
	Errcode err;
	LONG frame_oset;
	Rcel* rc;
	bool was_closed;
	bool allocd = false;

	if (fc->flags & FCEL_RAMONLY) /* no seeking on ram cel frames */
		return (Success);

	was_closed = (fc->flif.xf == NULL);
	if (was_closed) {
		/* note that this reports errors */
		err = reopen_fcelio(fc, XREADONLY);
		if (err < Success)
			goto error;
	}

	rc = fc->rc;

	/* wrap frame */
	frame = fli_wrap_frame(&fc->flif, frame);

	if (fc->frame_loaded != fc->cd.cur_frame) {
		/* re-seek from start of fli */
		frame_oset = fc->flif.hdr.frame1_oset;
		i		   = -1;
	} else if (frame > fc->cd.cur_frame) {
		frame_oset = fc->cd.next_frame_oset;
		i		   = fc->cd.cur_frame;
	} else if (frame < fc->cd.cur_frame) {
		if (fc->cd.cur_frame == fc->flif.hdr.frame_count - 1) {
			frame_oset = fc->cd.next_frame_oset;
			i		   = fc->cd.cur_frame;
		} else /* re seek from start */
		{
			frame_oset = fc->flif.hdr.frame1_oset;
			i		   = -1;
		}
	} else if (force_read) /* frame == cur_frame */
	{
		/* re-seek from start of fli */
		rc		   = NULL; /* no need to unfli it */
		frame_oset = fc->flif.hdr.frame1_oset;
		i		   = -1;
	} else
		goto done;

	if (cbuf == NULL) {
		if ((err = pj_fli_cel_alloc_cbuf(&cbuf, fc->rc)) < Success)
			goto error;
		allocd = true;
	}

	frame_oset = xffseek_tell(fc->flif.xf, frame_oset, XSEEK_SET);
	if (frame_oset < 0) {
		err = frame_oset;
		goto error;
	}

	while (i++ != frame) {
		if ((err = pj_fli_read_uncomp(NULL, &fc->flif, rc, cbuf, true)) < Success)
			goto error;

		if (i >= fc->flif.hdr.frame_count) {
			frame_oset = xffseek_tell(fc->flif.xf, fc->flif.hdr.frame2_oset, XSEEK_SET);
			if (frame_oset < 0) {
				err = frame_oset;
				goto error;
			}
			i = 0;
		} else {
			frame_oset += cbuf->size; /* add size of frame read in */
		}
	}

	if (frame == 0)
		fc->cd.next_frame_oset = fc->flif.hdr.frame2_oset;
	else
		fc->cd.next_frame_oset = frame_oset;

done:
	fc->frame_loaded = fc->cd.cur_frame = frame;
	err									= Success;
error:
	if (allocd)
		pj_freez(&cbuf);
	if (was_closed)
		close_fcelio(fc);
	return (err);
}

LONG fcel_cbuf_size(Flicel* fc)
{
	return (pj_fli_cbuf_size(fc->rc->width, fc->rc->height, fc->rc->cmap->num_colors));
}

bool fcel_needs_seekbuf(Flicel* fc)
{
	if ((fc->flags & FCEL_RAMONLY) ||
		(fc->frame_loaded == fc->cd.cur_frame && fc->flif.hdr.frame_count <= 1)) {
		return (false);
	}
	return (true);
}

Errcode seek_fcel_frame(Flicel* fc, SHORT frame)
{
	return (gb_seek_fcel_frame(fc, frame, NULL, false));
}

/* increment flicel frame */
Errcode inc_fcel_frame(Flicel* fc)
{
	return (seek_fcel_frame(fc, fc->cd.cur_frame + 1));
}

/* loads a cel stored away with a temp file extant as a newly allocated
 * flicel */
Errcode load_temp_fcel(char* tempname, Flicel** pfc)
{
	Errcode err;
	Fat_chunk fchunk;
	Flicel* fc;

	err = alloc_fcel(pfc);
	if (err < Success)
		return err;

	fc = *pfc;

	fc->tpath = clone_string(tempname);
	if (fc->tpath == NULL) {
		err = Err_no_memory;
		goto error;
	}

	err = pj_fli_open(tempname, &fc->flif, XREADONLY);
	if (err < Success) {
		goto error;
	}

	err = xffreadoset(fc->flif.xf, &fc->cd, CELDATA_OFFSET, sizeof(Celdata));
	if (err < Success) {
		goto error;
	}

	if (fc->cd.id.type != FP_CELDATA) {
		err = Err_corrupted;
		goto error;
	}

	err = alloc_fcel_raster(fc);
	if (err < Success) {
		goto error;
	}

	err = xffread(fc->flif.xf, &fchunk, sizeof(fchunk));
	if (err < Success) {
		goto error;
	}

	if (fchunk.type != FP_FLIPATH) {
		err = Err_corrupted;
		goto error;
	}

	thecel->cpath = pj_malloc(fchunk.size);
	if (thecel->cpath == NULL) {
		err = Err_no_memory;
		goto error;
	}

	thecel->cpath->id = fchunk;

	err = xffread(fc->flif.xf, OPTR(fc->cpath, sizeof(fchunk)), fchunk.size - sizeof(fchunk));
	if (err < Success) {
		goto error;
	}

	/* close temp file and open fli pointed to and
	 * verify it's the right one */

	pj_fli_close(&fc->flif);

	err = pj_fli_open(fc->cpath->path, &fc->flif, XREADONLY);
	if (err < Success) {
		goto error;
	}

	if (memcmp(&thecel->cpath->fid, &thecel->flif.hdr.id, sizeof(Fli_id)) ||
		thecel->flif.hdr.width != thecel->rc->width ||
		thecel->flif.hdr.height != thecel->rc->height) {
		err = softerr(Err_invalid_id, "!%s", "fcel_load", thecel->cpath->path);
		goto error;
	}
	refresh_flicel_pos(thecel);
	thecel->frame_loaded = -1;
	err = seek_fcel_frame(thecel, thecel->cd.cur_frame);
	if (err < Success) {
		goto error;
	}

	pj_fli_close(&thecel->flif);
	return Success;

error:
//	softerr(err, "!%s", "fcel_temp", tempname);
	free_fcel(pfc);
	return err;
}

/* attempts to load a pic file as a flicel with one frame putting pic
 * image in celfli_name and tempfile in tempname (if both are non NULL)
 * otherwise it makes a cel with the FCEL_RAMONLY flag set and no files */
static Errcode load_pic_fcel(char* pdr_name,
							 Anim_info* ainfo,
							 char* picpath,
							 char* tempname,
							 char* celfli_name,
							 Flicel** pfcel)
{
	Errcode err;
	Rcel* rc = NULL;

	if ((err = valloc_ramcel(&rc, ainfo->width, ainfo->height)) < Success)
		goto error;

	rc->x = ainfo->x;
	rc->y = ainfo->y;

	if ((err = pdr_load_picture(pdr_name, picpath, rc)) < Success)
		goto error;

	if ((err = make1_flicel(tempname, celfli_name, pfcel, rc)) < Success)
		goto error;

	if (celfli_name == NULL) {
		if ((err = alloc_flipath(picpath, NULL, &(*pfcel)->cpath)) < Success)
			goto error;
	}

	return (Success);
error:
	pj_rcel_free(rc);
	return (err);
}

/***** specific stuff to "thecel" *****/

/* Load a flicel, from any file source. Free cel pointed to by pfcel before
 * actually loading a new one. If *pfcel is not a cel it should be NULL.
 * If file is a variable resolution format it will take the vb.pencel as a
 * reference size, used in load_the_cel() and in the join menu to load
 * the cel to join. Asks to use non-fli multi frame file's first frame
 * but does not report all errors, searches for any pdr able to load fli
 * or still image type */
Errcode pdr_load_any_flicel(char* path, char* tempname, char* fliname, Flicel** pfcel)
{
	Errcode err;
	Anim_info ainfo;
	char pdr_name[PATH_SIZE];

	if ((err = find_pdr_loader(path, true, &ainfo, pdr_name, vb.pencel)) < Success)
		goto error;

	if (is_fli_pdr_name(pdr_name)) {
		free_fcel(pfcel);
		err = load_fli_fcel(path, tempname, fliname, pfcel);
		goto done;
	} else if (ainfo.num_frames > 1) {
		if (!soft_yes_no_box("!%s%d", "first_only", path, ainfo.num_frames)) {
			err = Err_abort;
			goto error;
		}
	}

	free_fcel(pfcel);
	err = load_pic_fcel(pdr_name, &ainfo, path, tempname, fliname, pfcel);

done:
error:
	return (err);
}

Errcode load_the_cel(char* path)
{
	Errcode err;

	err = pdr_load_any_flicel(path, cel_name, cel_fli_name, &thecel);
	return (cant_load(err, path));
}

Errcode go_load_the_cel(void)
{
	Errcode err;
	char suffi[PDR_SUFFI_SIZE * 2 + 10];
	char* title;
	char sbuf[50];

	get_celload_suffi(suffi);

	if ((title = vset_get_filename(
		   stack_string("load_cel", sbuf), suffi, load_str, CEL_PATH, NULL, 0)) != NULL) {
		unzoom();
		err = load_the_cel(title);
		rezoom();
	} else
		err = Err_abort;
	return (err);
}

void qload_the_cel(void)
{
	if (go_load_the_cel() >= 0) {
		show_thecel_a_sec();
	}
}

static Errcode save_the_cel(char* path)
{
	Errcode err;
	char* celpath;
	Flifile oflif;
	Chunkparse_data pd;
	LONG added_size;
	LONG rootsize;
	LONG chunksize;

	clear_struct(&oflif);
	pj_fli_close(&thecel->flif);
	if (!thecel->cpath)
		return (Err_bad_input);

	if (!strcmp(path, cel_name) /* no saving here */
		|| !strcmp(path, cel_fli_name)) {
		err = Err_in_use;
		goto error;
	}
	celpath = thecel->cpath->path;

	if (!strcmp(path, celpath)) {
		/* we are re-writing (updating only the cel chunk)
		 * in a file pointed to */
		err = pj_fli_open(celpath, &oflif, XREADWRITE_OPEN);
		if (err < Success)
			goto error;

		if (memcmp(&oflif.hdr.id, &thecel->cpath->fid, sizeof(Fli_id))) {
			/* if ids dont match we overwrite file */
			pj_fli_close(&oflif);
			goto overwrite_it;
		}

		added_size = 0;
		init_chunkparse(&pd, oflif.xf, FCID_PREFIX, sizeof(Fli_head), 0, 0);
		while (get_next_chunk(&pd)) {
			if (pd.type == (USHORT)ROOT_CHUNK_TYPE) {
				rootsize = pd.fchunk.size;
			} else if (pd.type == FP_CELDATA) {
				if (pd.fchunk.size != sizeof(Celdata)) {
					pd.fchunk.type = FP_FREE; /* declare it empty */

					err = xffwriteoset(pd.xf, &pd.fchunk, pd.chunk_offset, sizeof(Chunk_id));
					if (err < Success)
						goto error;

					pd.error = Success; /* Oh well, we put in a new one */
					break;
				}
				if ((err = update_parsed_chunk(&pd, &thecel->cd)) < Success)
					goto error;
				pd.error = 1;
				break;
			}
		}

		switch (pd.error) {
			case 1: /* re-wrote cel chunk successfully above */
				break;
			case Err_no_chunk: /* no prefix chunk */
			{
				/* we gotta install a new prefix chunk */
				added_size	   = sizeof(Celdata) + sizeof(Chunk_id);
				pd.fchunk.size = 0;
				goto insert_celchunk;
			}
			case Success: /* have a prefix chunk without cel chunk */
			{
				/* we gotta install a new cel chunk add space, and write it */

				/* actually this will move the old FCID_PREFIX fchunk toward
				 * eof but it will be re written and the old one will be
				 * overwritten by the new celdata */

				pd.fchunk.size = rootsize;
				added_size	   = sizeof(Celdata);

			insert_celchunk:
				err = pj_insert_space(oflif.xf, sizeof(Fli_head), added_size);
				if (err < Success)
					goto error;

				/* write or rewrite PREFIX fchunk */
				pd.fchunk.size += added_size;
				pd.fchunk.type = FCID_PREFIX;
				err			   = xffwrite(oflif.xf, &pd.fchunk, sizeof(Chunk_id));
				if (err < Success)
					goto error;

				/* write cel chunk */
				err = xffwrite(oflif.xf, &thecel->cd, sizeof(Celdata));
				if (err < Success)
					goto error;

				break;
			}
			default: /* err < Success */
				err = pd.error;
				goto error;
		}

		/* adjust offsets for any added prefix size */

		oflif.hdr.frame1_oset += added_size;
		oflif.hdr.frame2_oset += added_size;
		oflif.hdr.size += added_size;

		if ((err = pj_i_flush_head(&oflif)) < Success)
			goto error;
		pj_fli_close(&oflif);
		thecel->cpath->fid = oflif.hdr.id;		  /* update pointer to it */
		thecel->cd.next_frame_oset += added_size; /* adjust pointer to next
												   * frame for changes */
		goto done;
	}

overwrite_it:
	/* not the same file */
	err = pj_fli_open(celpath, &thecel->flif, XREADONLY);
	if (err < Success)
		goto error;

	err = pj_fli_create(path, &oflif);
	if (err < Success)
		goto error;

	/* fudge to save out cel aspect the same as current screen window */

	thecel->flif.hdr.aspect_dx = vb.pencel->aspect_dx;
	thecel->flif.hdr.aspect_dy = vb.pencel->aspect_dy;
	oflif.hdr				   = thecel->flif.hdr;

	err = jwrite_chunk(oflif.xf, &thecel->cd, sizeof(Celdata), FCID_PREFIX);
	if (err < Success)
		goto error;

	oflif.hdr.frame1_oset = xfftell(oflif.xf);
	if (err < Success) {
		err = oflif.hdr.frame1_oset;
		goto error;
	}

	oflif.hdr.frame2_oset =
	  oflif.hdr.frame1_oset + thecel->flif.hdr.frame2_oset - thecel->flif.hdr.frame1_oset;

	chunksize = thecel->flif.hdr.size - thecel->flif.hdr.frame1_oset;

	/* copy all frames to output fli file */
	err = pj_copydata_oset(
	  thecel->flif.xf, oflif.xf, thecel->flif.hdr.frame1_oset, oflif.hdr.frame1_oset, chunksize);
	if (err < Success)
		goto error;

	oflif.hdr.size = oflif.hdr.frame1_oset + chunksize;
	if ((err = pj_i_flush_head(&oflif)) < Success) {
		goto error;
	}
	goto done;

error:
	return (softerr(err, "!%s", "cant_save", path));

done:
	pj_fli_close(&oflif);
	pj_fli_close(&thecel->flif);
	return (err);
}

void qsave_the_cel(void)
{
	char* title;
	char sbuf[50];

	if (thecel == NULL)
		return;
	hide_mp();
	if ((title = vset_get_filename(
		   stack_string("save_cel", sbuf), ".CEL", save_str, CEL_PATH, NULL, 1)) == NULL) {
		goto out;
	}

	if (!overwrite_old(title))
		goto out;

	unzoom();
	save_the_cel(title);
	rezoom();

out:
	show_mp();
}
