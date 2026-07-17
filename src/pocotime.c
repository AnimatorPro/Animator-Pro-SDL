/* pocotime.c - time oriented poco functions.  Insert/delete frame.
   Set frame count. Do effect over multiple frames... */

#include "jimk.h"
#include "errcodes.h"
#include <stdio.h>
#include "pocoface.h"
#include "pocolib.h"
#include "auto.h"
#include "flx.h"
#include "render.h"

extern Errcode builtin_err;

void next_frame();
void prev_frame();
void set_flx_length(int frames);
Errcode delete_some(int x);

extern Errcode insert_frames(int count, int where); // from time.c


static void po_sleep(double seconds)
/*****************************************************************************
 * void sleep(double seconds)
 ****************************************************************************/
{
ULONG clock;

clock = pj_clock_1000() + seconds*1000;
while (clock > pj_clock_1000())
	;
}

static void po_playit(long frames)
/*****************************************************************************
 * void PlayFlic(long frames)
 ****************************************************************************/
{
flx_clear_olays(); /* undraw cels cursors etc */
scrub_cur_frame();
vp_playit(frames);
pj_rcel_copy(vb.pencel,undof);
flx_draw_olays(); /* restore cels and such */
}

static int po_get_speed(void)
/*****************************************************************************
 * int GetSpeed(void)
 ****************************************************************************/
{
return(flix.hdr.speed);
}

static void po_set_speed(int speed)
/*****************************************************************************
 * void SetSpeed(int speed)
 ****************************************************************************/
{
extern Qslider speed_sl;
int max = jiffies_to_millisec(speed_sl.max);
if (speed < 0)
	speed = 0;
if (speed > max)
	speed = max;
flix.hdr.speed = speed;
}

Errcode po_set_flx_length(int frames)
/*****************************************************************************
 * ErrCode SetFrameCount(int count)
 ****************************************************************************/
{
if (frames < 1)
	return Err_parameter_range;
set_flx_length(frames);
return Success;
}

int po_get_frame_position(void)
/*****************************************************************************
 * int GetFrame(void)
 ****************************************************************************/
{
return(vs.frame_ix);
}

int po_get_frame_count(void)
/*****************************************************************************
 * int GetFrameCount(void)
 ****************************************************************************/
{
return(flix.hdr.frame_count);
}

Errcode po_delete_frames(int count)
/*****************************************************************************
 * ErrCode DeleteFrames(int count)
 ****************************************************************************/
{
if (count < 1)
	return Err_parameter_range;
return delete_some(count);
}

Errcode po_insert_frames(int count)
/*****************************************************************************
 * ErrCode InsertFrames(int count)
 ****************************************************************************/
{
Errcode err;

if (count < 1)
	return Err_parameter_range;

if ((err = scrub_cur_frame()) < Success)
	return(err);
return(insert_frames(count, vs.frame_ix));
}

typedef struct poco1_dat
	{
	void *code;
	Popot pdata;  /* Popot reconstructed from raw void* for the Poco callback */
	} Poco1_dat;

Errcode poco1(Poco1_dat *pd, int ix, int total, int scale)
/*****************************************************************************
 * the 'do it' routine for Poco's OverTime() function. (I think).
 ****************************************************************************/
{
double time;
Errcode err;
Pt_num ret;
PocoCallbackValue callback_args[2];

time = 1.0 * scale / SCALE_ONE;

callback_args[0].kind = POCO_CALLBACK_VALUE_DOUBLE;
callback_args[0].value.double_value = time;
callback_args[1].kind = POCO_CALLBACK_VALUE_POPOT;
callback_args[1].value.popot_value = pd->pdata;
err = poco_invoke_callback(pd->code, &ret, callback_args, 2);

if ((builtin_err = err) >= Success)
	{
	err = ret.i;
	}
return(err);
}

static Errcode po_over_time(void* effect, void* data)
/*****************************************************************************
 * ErrCode OverTime(ErrCode (*effect)(double time, void *data), void *data)
 ****************************************************************************/
{
	Poco1_dat pd;
	Errcode err;
	int omulti;

	if (effect == NULL)
		return(builtin_err = Err_null_ref);
	pd.pdata = (Popot){data, NULL, NULL};
	if ((pd.code = po_fuf_code(effect)) == NULL)
		return(Err_function_not_found);
	free_render_cashes();
	omulti = vs.multi;
	vs.multi = true;
	err = do_autodraw(poco1,&pd);
	vs.multi = omulti;
	make_render_cashes();
	return(err);
}

static Errcode po_over_some(void* effect, void* data, enum automodes tmode)
/*****************************************************************************
 * This does a function over time without bringing up the Time Select panel.
 ****************************************************************************/
{
	Poco1_dat pd;
	Errcode err;
	Autoarg aa;

	if (effect == NULL)
		return(builtin_err = Err_null_ref);
	pd.pdata = (Popot){data, NULL, NULL};
	if ((pd.code = po_fuf_code(effect)) == NULL)
		return(Err_function_not_found);
	free_render_cashes();
	clear_struct(&aa);
	aa.avec = poco1;
	aa.flags = AUTO_UNZOOM;
	aa.avecdat = &pd;
	err = noask_do_auto(&aa, tmode);
	make_render_cashes();
	return(err);
}

static Errcode po_over_segment(void* effect, void* data)
/*****************************************************************************
 * ErrCode OverSegment(ErrCode (*effect)(double time, void *data), void *data)
 ****************************************************************************/
{
return(po_over_some(effect, data, DOAUTO_SEGMENT));
}

static Errcode po_over_all(void* effect, void* data)
/*****************************************************************************
 * ErrCode OverAll(ErrCode (*effect)(double time, void *data), void *data)
 ****************************************************************************/
{
return(po_over_some(effect, data, DOAUTO_ALL));
}

Errcode po_poe_overtime(void *effect, void *data)
/*****************************************************************************
 * the overtime routine used by poe modules.
 *	this takes a pointer to a real C routine, not a poco routine.
 ****************************************************************************/
{
	Errcode err;
	int 	omulti;

	if (effect == NULL)
		return(builtin_err = Err_null_ref);

	free_render_cashes();	/* no bills larger than $20 accepted after 5pm... */
	omulti = vs.multi;
	vs.multi = true;
	err = do_autodraw(effect,data);
	vs.multi = omulti;
	make_render_cashes();
	return(err);
}

static Errcode po_poe_over_some(void *effect, void *data, enum automodes tmode)
/*****************************************************************************
 * This does a function over time without bringing up the Time Select panel.
 *	 the POE version: takes a real C routine pointer, not a Poco routine ptr.
 ****************************************************************************/
{
	Errcode err;
	Autoarg aa;

	free_render_cashes();
	clear_struct(&aa);
	aa.avec = effect;
	aa.flags = AUTO_UNZOOM;
	aa.avecdat = data;
	err = noask_do_auto(&aa, tmode);
	make_render_cashes();
	return(err);
}

Errcode po_poe_oversegment(void *effect, void *data)
/*****************************************************************************
 * POE-accessible version of OverSegment
 ****************************************************************************/
{
	return po_poe_over_some(effect, data, DOAUTO_SEGMENT);
}

Errcode po_poe_overall(void *effect, void *data)
/*****************************************************************************
 * POE-accessible version of OverAll
 ****************************************************************************/
{
	return po_poe_over_some(effect, data, DOAUTO_ALL);
}

static void po_set_time_mode(bool is_multi)
{
vs.multi = is_multi;
}

static bool po_get_time_mode(void)
{
return(vs.multi);
}

static void po_set_fsa_mode(int fsa)
{
if (fsa < 0 || fsa > 2)
	{
	builtin_err = Err_parameter_range;
	}
vs.time_mode = fsa;
}

static int po_get_fsa_mode(void)
{
return(vs.time_mode);
}

static int clip_frame(int frame)
{
if (frame < 0)
	frame = 0;
else if (frame >= flix.hdr.frame_count)
	frame = flix.hdr.frame_count-1;
return(frame);
}

static void po_set_seg_start(int frame)
{
vs.start_seg = clip_frame(frame);
}

static int po_get_seg_start(void)
{
return(vs.start_seg);
}

static void po_set_seg_end(int frame)
{
vs.stop_seg = clip_frame(frame);
}

static int po_get_seg_end(void)
{
return(vs.stop_seg);
}

static void set_still(bool still)
{
vs.ado_tween = !still;
}

static bool get_still(void)
{
return(!vs.ado_tween);
}

static void set_in_slow(bool in_slow)
{
vs.ado_ease = in_slow;
}

static bool get_in_slow(void)
{
return(vs.ado_ease);
}

static void set_out_slow(bool out_slow)
{
vs.ado_ease_out = out_slow;
}

static bool get_out_slow(void)
{
return(vs.ado_ease_out);
}

static void set_ping_pong(bool ping_pong)
{
vs.ado_pong = ping_pong;
}

static bool get_ping_pong(void)
{
return(vs.ado_pong);
}

static void set_reverse(bool reverse)
{
vs.ado_reverse = reverse;
}

static bool get_reverse(void)
{
return(vs.ado_reverse);
}

static void set_complete(bool complete)
{
vs.ado_complete = complete;
}

static bool get_complete(void)
{
return(vs.ado_complete);
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

PolibTime po_libtime = {
pj_clock_1000,
	"long    Clock1000(void);",
po_sleep,
	"void    sleep(double seconds);",
next_frame,
	"void    NextFrame(void);",
prev_frame,
	"void    BackFrame(void);",
flx_seek_frame,
	"void    SetFrame(int frame);",
po_get_frame_position,
	"int     GetFrame(void);",
po_set_flx_length,
	"ErrCode SetFrameCount(int count);",
po_get_frame_count,
	"int     GetFrameCount(void);",
po_playit,
	"void    PlayFlic(long frames);",
po_set_speed,
	"void    SetSpeed(int speed);",
po_get_speed,
	"int     GetSpeed(void);",
po_insert_frames,
	"ErrCode InsertFrames(int count);",
po_delete_frames,
	"ErrCode DeleteFrames(int count);",
po_over_time,
	"ErrCode OverTime(ErrCode (*effect)(double time, void *data), void *data);",
po_over_all,
	"ErrCode OverAll(ErrCode (*effect)(double time, void *data), void *data);",
po_over_segment,
	"ErrCode OverSegment(ErrCode (*effect)(double time, void *data), void *data);",

po_set_time_mode,
	"void    SetTimeSelect(Boolean is_multi);",
po_get_time_mode,
	"Boolean GetTimeSelect(void);",
po_set_fsa_mode,
	"void    SetFSA(int fsa);",
po_get_fsa_mode,
	"int     GetFSA(void);",
po_set_seg_start,
	"void    SetSegStart(int frame);",
po_get_seg_start,
	"int     GetSegStart(void);",
po_set_seg_end,
	"void    SetSegEnd(int frame);",
po_get_seg_end,
	"int     GetSegEnd(void);",
set_still,
	"void    SetStill(Boolean still);",
get_still,
	"Boolean GetStill(void);",
set_in_slow,
	"void    SetInSlow(Boolean InSlow);",
get_in_slow,
	"Boolean GetInSlow(void);",
set_out_slow,
	"void    SetOutSlow(Boolean OutSlow);",
get_out_slow,
	"Boolean GetOutSlow(void);",
set_ping_pong,
	"void    SetPingPong(Boolean PingPong);",
get_ping_pong,
	"Boolean GetPingPong(void);",
set_reverse,
	"void    SetReverse(Boolean reverse);",
get_reverse,
	"Boolean GetReverse(void);",
set_complete,
	"void    SetComplete(Boolean complete);",
get_complete,
	"Boolean GetComplete(void);",
};

Poco_lib po_time_lib = {
	NULL, "Time Oriented Function",
	(Lib_proto *)&po_libtime, POLIB_TIME_SIZE,
	};
