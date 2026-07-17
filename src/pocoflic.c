/*****************************************************************************
 * pocoflic.c - The flic playback library.
 ****************************************************************************/

#include "errcodes.h"
#include "pjbasics.h"
#include "fli.h"
#include "pocolib.h"
#include "jimk.h"


/*----------------------------------------------------------------------------
 * Local types and data...
 *--------------------------------------------------------------------------*/

typedef struct poco_event_data {	/* what we pass to a Poco callback func,  */
	Popot	pflic;					/* kept in a structure for easy pass-by-  */
	Popot	userdata;				/* value to the Poco function.	We also   */
	long	cur_loop;				/* use the cur_loop, cur_frame, and 	  */
	long	cur_frame;				/* and num_frames internally during 	  */
	long	num_frames; 			/* playback.							  */
} PocoEventData;

typedef struct flic {				 /* this is a custom Flic type used only  */
	struct flic   *next;			 /* within this module.  It's not related */
	ULONG		  magic;			 /* in any way to 'Flic' types you may    */
	Flifile 	  *flifile; 		 /* find elsewhere. 					  */
	Rcel		  *root_raster;
	Rcel		  *playback_raster;
	void		  *framebuf;
	int 		  speed;
	bool 	  see_mouse;
	bool 	  input_stops_playback;
	int 		  frames_played;		/* a shortcut for play_count detector */
	long		  eventdata;			/* event data for internal detectors  */
	PocoEventData event_data;			/* event data for poco callback funcs */
	void		  *poco_func;			/* pointer to poco callback fuf 	  */
} Flic;

#define IANS_FLIC_MAGIC 	0x19040259	/* validates legal Flic structure	  */
#define BEFORE_FIRST_FRAME	-1			/* indicates haven't started playback */

typedef bool (EventFunc)(Flic *pflic);

static Flic *fliclist = NULL;	/* resource list for unload-time cleanup */

/*----------------------------------------------------------------------------
 * a few basic service routines...
 *--------------------------------------------------------------------------*/

/*****************************************************************************
 * free the playback raster, if it is a virtual raster built over the root.
 ****************************************************************************/
static void free_playback_raster(Flic *pflic)
{
	if (NULL == pflic || NULL == pflic->playback_raster) {
		return;
	}

	if (pflic->playback_raster != pflic->root_raster) {
		pj_free(pflic->playback_raster);
		pflic->playback_raster = NULL;
	}
}

/*****************************************************************************
 * make a playback raster.	vb.pencel is used unless the flic is a different
 * size, in which case a virtual raster is built over vb.pencel.
 ****************************************************************************/
static Errcode build_playback_raster(Flic *pflic, Rcel *root, int x, int y)
{
	Flifile 	*flifile;
	Rectangle	therect;

	if (NULL == pflic) {
		return Err_null_ref;
	}

	flifile = pflic->flifile;
	if (flifile == NULL) {
		return Err_file_not_open;
	}

	free_playback_raster(pflic);	/* free current raster, if any */

	if (NULL == root) {
		if (NULL == pflic->root_raster) {
			pflic->root_raster = vb.pencel;
		}
		root = pflic->root_raster;
	} else {
		pflic->root_raster = root;
	}

	if (root->width == flifile->hdr.width
	 && root->height == flifile->hdr.height
	 && x == 0
	 && y == 0) {
		pflic->playback_raster = root;
	} else {
		if (x == 0 && y == 0) {
			therect.x = (root->width  - flifile->hdr.width)  / 2;
			therect.y = (root->height - flifile->hdr.height) / 2;
		} else {
			therect.x = x;
			therect.y = y;
		}
		therect.width  = flifile->hdr.width;
		therect.height = flifile->hdr.height;
		pflic->playback_raster = pj_malloc(sizeof(Rcel));
		if (pflic->playback_raster == NULL) {
			return Err_no_memory;
		}
		pj_rcel_make_virtual(pflic->playback_raster, root, &therect);
	}

	return Success;
}

/*****************************************************************************
 * I'm not sure this is the best way to do this, but...
 ****************************************************************************/
static bool any_user_input(void)
{
	wait_wndo_input(ANY_INPUT);
	return ISDOWN(MBPEN|MBRIGHT) || JSTHIT(KEYHIT);
}

/*****************************************************************************
 * report a fatal internal error, and set builtin_err to Err_reported.
 *
 * in development mode, this gets verbose with us.	normally, it just
 * sets the builtin error code, but punts the (hardcoded English-language)
 * extended error message.
 ****************************************************************************/
static Errcode internal_error(Errcode err, char *fmt, ...)
{
#ifdef DEVELOPMENT

	char etext[ERRTEXT_SIZE];
	va_list args;
	bool mouse_was_on;

	if (err >= Success)
		return err;

	if(!get_errtext(err,etext))
		return builtin_err = err;

	mouse_was_on = show_mouse();
	va_start(args, fmt);
	varg_continu_box(NULL,fmt,args,etext);
	va_end(args);
	if (!mouse_was_on)
		hide_mouse();

	return builtin_err = Err_reported;

#else
	(void)fmt;
	return builtin_err = err;

#endif /* DEVELOPMENT */

}

/*****************************************************************************
 * make sure the Flic* we got points to a valid Flic structure.
 * (ie, make sure we didn't get a recast pointer to some other datatype)
 ****************************************************************************/
static Errcode flic_integrity_check(Flic *pflic)
{
	if (NULL == pflic) {
		return builtin_err = Err_null_ref;
	}

	if (IANS_FLIC_MAGIC != pflic->magic) {
		return internal_error(Err_wrong_type,
			"Flic handle doesn't point to a valid Flic structure");
	}

	if (NULL == pflic->flifile) {
		return internal_error(Err_file_not_open,
			"Flifile structure not attached to Flic structure");
	}

	if (NULL == pflic->root_raster) {
		return internal_error(Err_null_ref, "NULL root_raster");
	}

	if (NULL == pflic->playback_raster) {
		return internal_error(Err_null_ref, "NULL playback_raster");
	}

	if (NULL == pflic->framebuf) {
		return internal_error(Err_null_ref, "NULL framebuf pointer");
	}

	return Success;
}

/*----------------------------------------------------------------------------
 * the guts of the playback: event detectors and play_until()...
 *--------------------------------------------------------------------------*/

/*****************************************************************************
 * call the Poco event detector routine, return its stop/continue status.
 *
 *	The call to the poco function appears (to the poco func) as:
 *
 *		func(Flic *flic, void *userdata,
 *			 long cur_loop, long cur_frame, long num_frames);
 *
 *	The typed callback entry receives those five values in source order.
 ****************************************************************************/
static bool until_poco_event(Flic *pflic)
{
	Errcode err;
	Pt_num	ret;
	PocoCallbackValue callback_args[5];

	callback_args[0].kind = POCO_CALLBACK_VALUE_POPOT;
	callback_args[0].value.popot_value = pflic->event_data.pflic;
	callback_args[1].kind = POCO_CALLBACK_VALUE_POPOT;
	callback_args[1].value.popot_value = pflic->event_data.userdata;
	callback_args[2].kind = POCO_CALLBACK_VALUE_LONG;
	callback_args[2].value.long_value = pflic->event_data.cur_loop;
	callback_args[3].kind = POCO_CALLBACK_VALUE_LONG;
	callback_args[3].value.long_value = pflic->event_data.cur_frame;
	callback_args[4].kind = POCO_CALLBACK_VALUE_LONG;
	callback_args[4].value.long_value = pflic->event_data.num_frames;
	err = poco_invoke_callback(pflic->poco_func, &ret, callback_args, 5);

	if (err != Success) {
		builtin_err = err;
		return false;		/* stop playback on internal error */
	} else {
		return ret.i;		/* return status from poco routine */
	}
}

/*****************************************************************************
 * event-detector for flic_play, ends playback when a key/mousebtn is hit.
 ****************************************************************************/
static bool until_input(Flic *notused)
{
	(void)notused;

	if (any_user_input()) {
		return false;	/* stop playback */
	}
	else {
		return true;	/* continue playback */
	}
}

/*****************************************************************************
 * event-detector for flic_play_timed, stops after timer exceeds expiry.
 ****************************************************************************/
static bool until_time_expires(Flic *pflic)
{
	if (pflic->eventdata < pj_clock_1000()){
		return false;	// clock exceeds expiry time, stop the flic
		}
	else {
		return true;	// keep playing
		}
}

/*****************************************************************************
 * event-detector for flic_play_once, stops after one time through flic.
 ****************************************************************************/
static bool until_once_through(Flic *pflic)
{
	return pflic->event_data.cur_frame < pflic->event_data.num_frames-1;
}

/*****************************************************************************
 * event-detector for flic_play_count, stops after count is reached.
 ****************************************************************************/
static bool until_frame_count(Flic *pflic)
{
	return pflic->frames_played < pflic->eventdata;
}

/*****************************************************************************
 * play a flic until the caller-specified event routine returns false to stop.
 ****************************************************************************/
static Errcode play_until(Flic *pflic, EventFunc *event_detect)
{
	Errcode 		err;
	ULONG			clock;
	Flifile 		*flif;
	Fli_head		*flihdr;
	bool 		stop_the_playback;
	bool 		mouse_was_on;

	/*------------------------------------------------------------------------
	 * do some misc setup before starting the actual playback...
	 *----------------------------------------------------------------------*/

	if (pflic->see_mouse) {
		mouse_was_on = show_mouse();
	} else {
		mouse_was_on = hide_mouse();
	}

	if (pflic->root_raster == vb.pencel) {
		dirties();
	}

	flif   = pflic->flifile;
	flihdr = &flif->hdr;

	if (pflic->speed < 0) {
		pflic->speed = flihdr->speed;
	}

	/*------------------------------------------------------------------------
	 * do the playback...
	 *----------------------------------------------------------------------*/

	stop_the_playback = false;

	do	{

		/*--------------------------------------------------------------------
		 * the first time through, or when the poco event detector has called
		 * FlicRewind(), cur_frame is BEFORE_FIRST_FRAME, and we have to
		 * seek back to frame 1 (the brun frame).
		 *------------------------------------------------------------------*/

        if (pflic->event_data.cur_frame == BEFORE_FIRST_FRAME) {
            /* Seek to first frame using xfile API */
            (void)xffseek_tell(flif->xf, flihdr->frame1_oset, XSEEK_SET);
            pflic->event_data.cur_frame = 0;
        } else {
			++pflic->event_data.cur_frame;
		}

		/*--------------------------------------------------------------------
		 * render the frame.
		 * get the current clock first, so that the delta time between frames
		 * includes the time it takes to render the frame.
		 *------------------------------------------------------------------*/

		clock = pflic->speed + pj_clock_1000();

		err = pj_fli_read_uncomp(NULL, flif,
								pflic->playback_raster, pflic->framebuf,true);
		if(Success > err) {
			goto ERROR_EXIT;
		}

		/*--------------------------------------------------------------------
		 * if the frame we just displayed is the ring frame, we increment
		 * the cur_loop counter, and set the cur_frame to zero.  that's
		 * because displaying the ring frame is really a fast way to display
		 * frame zero without un-brun'ing it again.  we also seek back to
		 * the second frame at this point, so that if the event detector kicks
		 * us out and we're called again later, we'll be properly positioned.
		 *------------------------------------------------------------------*/

		++pflic->frames_played;

        if (pflic->event_data.cur_frame == pflic->event_data.num_frames) {
            /* Seek to second frame using xfile API */
            (void)xffseek_tell(flif->xf, flihdr->frame2_oset, XSEEK_SET);
            ++pflic->event_data.cur_loop;
            pflic->event_data.cur_frame = 0;
        }

		/*--------------------------------------------------------------------
		 * call the event detector repeatedly, until it requests a stop, or
		 * it's time to display the next frame.  also check the keyboard
		 * after each event_detect() call, if the caller has asked for that.
		 *------------------------------------------------------------------*/

		do	{
			if (false == event_detect(pflic)
			 || (pflic->input_stops_playback && any_user_input())) {
				stop_the_playback = true;
			}
		} while (clock >= pj_clock_1000() && !stop_the_playback);

	} while (!stop_the_playback);

	err = Success;

ERROR_EXIT:

	if (pflic->see_mouse) {
		if (!mouse_was_on) {
			hide_mouse();
		}
	} else {
		if (mouse_was_on) {
			show_mouse();
		}
	}

	return internal_error(err, "Error reported by play_until()");
}

/*----------------------------------------------------------------------------
 * routines to open, close, rewind, and seek flics, and set playback options...
 *	these are called after the parms passed in from Poco are validated.
 *--------------------------------------------------------------------------*/

/*****************************************************************************
 * close flic file, free all associated resources, remove from resource list.
 ****************************************************************************/
static void do_flic_close(Flic *pflic)
{
	Flic *cur, **prev;

	if (NULL == pflic) {
		return;
	}

	if (NULL != pflic->flifile) {
		pj_fli_close(pflic->flifile);
		pj_free(pflic->flifile);
	}

	if (NULL != pflic->framebuf) {
		pj_free(pflic->framebuf);
	}

	free_playback_raster(pflic);

	for (prev = &fliclist, cur = fliclist; cur != NULL; cur = cur->next) {
		if (cur == pflic) {
			*prev = cur->next;
			break;
		}
		prev = &cur->next;
	}

	pflic->magic = 0xDEADDEAD;	/* prevent re-use */

	pj_free(pflic);
}

/*****************************************************************************
 * library-unload cleanup routine, close and free any open flics.
 ****************************************************************************/
static void do_flic_close_all(void *unused)
{
	Flic	*cur, *next;

	for (cur = fliclist; cur != NULL; cur = next) {
		next = cur->next;
		do_flic_close(cur);
	}
}

/*****************************************************************************
 * alloc flic control structures, open flic file, return status.
 ****************************************************************************/
static Errcode do_flic_open(char *path, Flic **ppflic)
{
	Errcode err;
	Flic	*pflic;
	Flifile *flifile;

	if ('\0' == *path) {
		return Err_parameter_range;
	}

	err = Err_no_memory;

	if (NULL == (pflic = pj_zalloc(sizeof(Flic)))) {
		goto ERROR_EXIT;
	}

	if (NULL == (flifile = pflic->flifile = pj_zalloc(sizeof(Flifile)))) {
		goto ERROR_EXIT;
	}

	if (Success > (err = pj_fli_open(path, pflic->flifile, JREADONLY))) {
		goto ERROR_EXIT;
	}

	err = pj_fli_alloc_cbuf(&pflic->framebuf,
							flifile->hdr.width, flifile->hdr.height, COLORS);
	if (Success > err) {
		goto ERROR_EXIT;
	}

	err = build_playback_raster(pflic, NULL, 0, 0);
	if (Success > err) {
		goto ERROR_EXIT;
	}

	/* init things in Flic that don't start out as zero... */

	pflic->magic				 = IANS_FLIC_MAGIC;
	pflic->event_data.cur_frame  = BEFORE_FIRST_FRAME;
	pflic->event_data.num_frames = flifile->hdr.frame_count;
	pflic->speed				 = flifile->hdr.speed;

	pflic->next = fliclist; 	/* add to resource list */
	fliclist = pflic;

	*ppflic = pflic;
	return Success;

ERROR_EXIT:

	do_flic_close(pflic);	/* free anything we managed to aquire */
	*ppflic = NULL;
	return err;

}

/*****************************************************************************
 * rewind flic; makes next playback call start at first frame.
 ****************************************************************************/
static void do_rewind(Flic *pflic)
{
	pflic->event_data.cur_frame  = BEFORE_FIRST_FRAME;
	pflic->event_data.cur_loop	 = 0;
	pflic->frames_played		 = 0;
}

/*****************************************************************************
 * change any or all of the playback options.
 ****************************************************************************/
static Errcode do_flic_options(Flic *pflic,
							   int	speed,
							   int	input_stops_playback,
							   int	see_mouse,
							   Rcel *new_raster,
							   int	x,
							   int	y)
{
	Errcode err;

	if (NULL == pflic) {
		return Err_null_ref;
	}

	if (speed >= 0) {
		pflic->speed = speed;
	}

	if (input_stops_playback >= 0) {
		pflic->input_stops_playback = input_stops_playback;
	}

	if (see_mouse >= 0) {
		pflic->see_mouse = see_mouse;
	}

	if (NULL != new_raster || x != 0 || y != 0) {
		err = build_playback_raster(pflic, new_raster, x, y);
		if (Success > err) {
			return err;
		}
		do_rewind(pflic); /* force rewind if the raster moved/changed */
	}

}

/*****************************************************************************
 * play the named flic until the poco event detector says to stop.
 ****************************************************************************/
static Errcode do_play_until(Flic *pflic, void *pocofunc, Popot userdata)
{
	pflic->poco_func		   = pocofunc;
	pflic->event_data.pflic    = po_ptr2ppt(pflic, sizeof(Flic));
	pflic->event_data.userdata = userdata;

	return play_until(pflic, until_poco_event);
}

/*****************************************************************************
 * play the named flic until a key is hit.
 ****************************************************************************/
static Errcode do_play(Flic *pflic)
{
	return play_until(pflic, until_input);
}

/*****************************************************************************
 * play a flic for the specified length of time.
 ****************************************************************************/
static Errcode do_play_timed(Flic *pflic, ULONG for_milliseconds)
{
	pflic->eventdata = for_milliseconds + pj_clock_1000();
	return play_until(pflic, until_time_expires);
}

/*****************************************************************************
 * play a flic once then stop.
 ****************************************************************************/
static Errcode do_play_once(Flic *pflic)
{
	return play_until(pflic, until_once_through);
}

/*****************************************************************************
 * play the specified number of frames.
 ****************************************************************************/
static Errcode do_play_count(Flic *pflic, int frames_to_play)
{
	if (frames_to_play == 0) {
		return Success;
	}

	if (frames_to_play < 0) {
		do_rewind(pflic);
		frames_to_play = -frames_to_play;
	}

	pflic->eventdata = pflic->frames_played + frames_to_play;

	return play_until(pflic, until_frame_count);
}

/*****************************************************************************
 * seek to the specified frame in the flic.  (yuck!  but it's really needed)
 ****************************************************************************/
static void do_seek_frame(Flic *pflic, int the_frame)
{
	int 	play_count;
	long	cur_frame;
	int 	original_speed;
	void	*original_raster;
	Rcel	*seek_raster;
	bool seek_raster_used;

	/*------------------------------------------------------------------------
	 * zero out the cur_loop counter.  doing this here ensures it is
	 * consistantly zeroed whether we need to rewind() or not during seek.
	 *----------------------------------------------------------------------*/

	pflic->event_data.cur_loop = 0;

	/*------------------------------------------------------------------------
	 * get the current frame into a local var.
	 * if we're already at the requested frame, just punt.
	 *----------------------------------------------------------------------*/

	cur_frame = pflic->event_data.cur_frame;
	if (the_frame == cur_frame) {
		return;
	}

	/*------------------------------------------------------------------------
	 * calc the number of frames we have to play to get from where we're at
	 * to where the caller wants to be.  if the requested frame is before
	 * the current frame, we do a rewind before falling into the playback.
	 *----------------------------------------------------------------------*/

	if (the_frame < cur_frame) {
		play_count = ++the_frame;
		do_rewind(pflic);
	} else if (cur_frame == BEFORE_FIRST_FRAME) {
		play_count = the_frame+1;
	} else {
		play_count = the_frame-cur_frame;
	}

	/*------------------------------------------------------------------------
	 * save the current playback speed and raster.
	 *
	 * allocate a temp ram raster to use for the seeking, and set the speed
	 * to zero.  if the raster allocation fails, no problem, we'll just end
	 * up seeking on the playback raster, which may look a bit ugly if the
	 * the playback raster is the video screen, but it's better than failing.
	 *
	 * we skip using the temp raster if the root raster is already a ram
	 * raster, or if we're only going to be playing 1 frame to get to the
	 * requested location.
	 *----------------------------------------------------------------------*/

	original_speed	 = pflic->speed;
	original_raster  = pflic->playback_raster;
	seek_raster_used = false;
	pflic->speed	 = 0;

	if (pflic->root_raster->type != RT_BYTEMAP && play_count > 1) {
		if (Success <= pj_rcel_bytemap_alloc(original_raster, &seek_raster, COLORS)) {
			pj_rcel_copy(original_raster, seek_raster);
			pflic->playback_raster = seek_raster;
			seek_raster_used = true;
		}
	}

	/*------------------------------------------------------------------------
	 * do the playback to effect the seek.	if the playback is onto the
	 * temp raster, copy the final frame back to the playback raster, then
	 * free the temp raster.  restore the original speed and raster to the
	 * Flic structure.
	 *----------------------------------------------------------------------*/

	do_play_count(pflic, play_count);

	if (seek_raster_used) {
		pj_rcel_copy(seek_raster, original_raster);
		pj_rcel_free(seek_raster);
	}

	pflic->speed		   = original_speed;
	pflic->playback_raster = original_raster;
}

/*****************************************************************************
 * return each of the flicinfo values for which we got a non-NULL pointer.
 *	(service routine for FlicInfo() and FlicOpenInfo())
 ****************************************************************************/
static Errcode return_flic_info(Flic *pflic,
								int* pwidth, int* pheight,
								int* pspeed, int* pframes)
{
	Flifile *flifile;

	if (NULL == pflic || NULL == pflic->flifile) {
		return internal_error(Err_null_ref,
			"Flic file not properly opened, cannot get info.");
	}

	flifile = pflic->flifile;

	if (NULL != pwidth) {
		*pwidth = flifile->hdr.width;
	}

	if (NULL != pheight) {
		*pheight = flifile->hdr.height;
	}

	if (NULL != pspeed) {
		*pspeed = flifile->hdr.speed;
	}

	if (NULL != pframes) {
		*pframes = flifile->hdr.frame_count;
	}

	return Success;
}

/*----------------------------------------------------------------------------
 * interface routines...
 *	 these are the functions that are mapped to the Poco FlicWhatever()
 *	 functions.  they validate pointers and whatnot, then call the above
 *	 routines to get the real work done.
 *--------------------------------------------------------------------------*/

/*****************************************************************************
 * Flic *FlicOpen(char *path)
 ****************************************************************************/
static void* flic_open(char* path)
{
	Errcode err;
	Flic	*pflic;

	if (NULL == path) {
		builtin_err = Err_null_ref;
		return NULL;
	}

	err = do_flic_open(path, &pflic);
	if (Success > err) {
		builtin_err = err;
		return NULL;
	}

	return pflic;
}

/*****************************************************************************
 * Errcode FlicInfo(char *path, int *w, int *h, int *speed, int *frames)
 *
 * this is the *only* FLICPLAY function that does not abort the Poco program
 * if the flic file can't be opened.  instead, it returns an error status.
 ****************************************************************************/
static Errcode flic_info(char* path, int* width, int* height, int* speed, int* frames)
{
	Errcode err;
	Flic	*pflic;

	if (NULL == path) {
		return builtin_err = Err_null_ref;
	}

	err = do_flic_open(path, &pflic);
	if (Success > err) {
		return err;
	}

	err = return_flic_info(pflic, width, height, speed, frames);

	do_flic_close(pflic);

	return err;
}

/*****************************************************************************
 * Flic *FlicOpenInfo(char *path, int *w, int *h, int *speed, int *frames)
 ****************************************************************************/
static void* flic_open_info(char* path, int* width, int* height, int* speed, int* frames)
{
	void *pflic;

	pflic = flic_open(path);
	if (pflic != NULL && Success <= builtin_err) {
		return_flic_info(pflic, width, height, speed, frames);
	}
	return pflic;
}

/*****************************************************************************
 * void FlicClose(Flic *pflic)
 ****************************************************************************/
static void flic_close(void* theflic)
{
	if (Success > flic_integrity_check(theflic)) {
		return;
	}
	do_flic_close(theflic);
}

/*****************************************************************************
 * void FlicRewind(Flic *pflic)
 ****************************************************************************/
static void flic_rewind(void* theflic)
{
	if (Success > flic_integrity_check(theflic)) {
		return;
	}
	do_rewind(theflic);
}

/*****************************************************************************
 * void FlicSeekFream(Flic *pflic, int toframe)
 ****************************************************************************/
static void flic_seek_frame(void* theflic, int theframe)
{
	Flic *pflic = theflic;

	if (Success > flic_integrity_check(pflic)) {
		return;
	}

	if (theframe < 0 || theframe > pflic->event_data.num_frames-1) {
		builtin_err = Err_parameter_range;
		return;
	}

	do_seek_frame(theflic, theframe);
}

/*****************************************************************************
 * void FlicOptions(Flic *f, int s, int input_stops, Screen *s, int x int y)
 ****************************************************************************/
static void flic_play_options(void* theflic,
					   int speed, int input_stops, int see_mouse,
					   void* screen, int x, int y)
{
	if (Success > flic_integrity_check(theflic)) {
		return;
	}

	builtin_err = do_flic_options(theflic, speed, input_stops, see_mouse, screen, x, y);
}

/*****************************************************************************
 * void FlicPlayUntil(Flic *flic, EventFunc *pocofunc, void *userdata)
 ****************************************************************************/
static void flic_play_until(void* theflic, void* eventfunc, void* userdata)
{
	void	*fuf;
	Flic	*pflic;
	Popot userdata_ppt;

	if (Success > flic_integrity_check(theflic)) {
		return;
	}
	pflic = theflic;

	if (NULL == eventfunc) {
		builtin_err = Err_null_ref;
		return;
	}

	if (NULL == (fuf = po_fuf_code(eventfunc))) {
		builtin_err = Err_function_not_found;
		return;
	}

	/* Wrap userdata in a Popot for the callback */
	Popot_make_null(&userdata_ppt);
	userdata_ppt.pt = userdata;
	builtin_err = do_play_until(theflic, fuf, userdata_ppt);
}

/*****************************************************************************
 * void FlicPlay(Flic *pflic)
 ****************************************************************************/
static void flic_play(void* theflic)
{
	if (Success > flic_integrity_check(theflic)) {
		return;
	}

	builtin_err = do_play(theflic);
}

/*****************************************************************************
 * void FlicPlay(Flic *pflic)
 ****************************************************************************/
static void flic_play_once(void* theflic)
{
	if (Success > flic_integrity_check(theflic)) {
		return;
	}

	builtin_err = do_play_once(theflic);
}

/*****************************************************************************
 * void FlicPlay(Flic *pflic, int millisecs)
 ****************************************************************************/
static void flic_play_timed(void* theflic, int milliseconds)
{
	if (Success > flic_integrity_check(theflic)) {
		return;
	}

	builtin_err = do_play_timed(theflic, milliseconds);
}

/*****************************************************************************
 * void FlicPlay(Flic *pflic, int count)
 ****************************************************************************/
static void flic_play_count(void* theflic, int frame_count)
{
	if (Success > flic_integrity_check(theflic)) {
		return;
	}

	builtin_err = do_play_count(theflic, frame_count);
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

PolibFlicPlay po_libflicplay = {

	NULL,			   "typedef struct __flic_handle__ Flic;",

	flic_info,		   "Errcode FlicInfo(char *path, int *width, "
									"int *height, int *speed, int *frames);",
	flic_open_info,    "Flic    *FlicOpenInfo(char *path, int *width, "
									"int *height, int *speed, int *frames);",
	flic_open,		   "Flic    *FlicOpen(char *path);",
	flic_close, 	   "void    FlicClose(Flic *theflic);",
	flic_rewind,	   "void    FlicRewind(Flic *theflic);",
	flic_seek_frame,   "void    FlicSeekFrame(Flic *theflic, int theframe);",
	flic_play_options, "void    FlicOptions(Flic *theflic, "
									"int speed, int input_stops_playback, "
									"int see_mouse, Screen *playback_screen, "
									"int xoffset, int yoffset);",
	flic_play,		   "void    FlicPlay(Flic *theflic);",
	flic_play_once,    "void    FlicPlayOnce(Flic *theflic);",
	flic_play_timed,   "void    FlicPlayTimed(Flic *theflic, int milliseconds);",
	flic_play_count,   "void    FlicPlayCount(Flic *theflic, int frame_count);",

	flic_play_until,   "void    FlicPlayUntil(Flic *theflic, "
									"int (*eventfunc)(Flic *flic, "
										"void *userdata, "
										"long cur_loop, long cur_frame, "
										"long num_frames), "
									"void *userdata);",

};

Poco_lib po_flicplay_lib =
	{
	NULL,
	"Flic Playback",
	(Lib_proto *)&po_libflicplay,
	POLIB_FLICPLAY_SIZE,
	NOFUNC, 				/* init func */
	do_flic_close_all,		/* cleanup func */
	NULL,					/* resource pointer (not used) */
	};
