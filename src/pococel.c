
#include "errcodes.h"
#include "ptrmacro.h"
#include "flicel.h"
#include "pocolib.h"
#include "render.h"

/* Must follow every header that still declares the old global. */
#include "ani_builtin_err.h"

extern Flicel* thecel;
extern Errcode cel_from_rect(Rectangle* rect, bool render_only);
extern Errcode clip_cel(void);
extern Errcode render_thecel();  // from flicel.c

/* Cel oriented stuff */
/*****************************************************************************
 * void CelPaste(void);
 ****************************************************************************/
static void po_ink_paste(void)
{
	free_render_cashes(); /* annoying but necessary */
	render_thecel();
	make_render_cashes();
	dirties();
}

/*****************************************************************************
 * ErrCode CelGet(int x, int y, int width, int height);
 *****************************************************************************/
static Errcode po_get_cel(int x, int y, int width, int height)
{
	Rectangle rect;

	rect.x = x;
	rect.y = y;
	rect.width = width;
	rect.height = height;
	return cel_from_rect(&rect, true);
}

/*****************************************************************************
 * void CelMove(int dx, int dy);
 *****************************************************************************/
static void po_move_cel(int dx, int dy)
{
	if (!thecel) {
		return;
	}
	translate_flicel(thecel, dx, dy);
	maybe_ref_flicel_pos(thecel);
}

/*****************************************************************************
 * void CelMoveTo(int x, int y);
 ****************************************************************************/
static void po_move_cel_to(int x, int y)
{
	if (!thecel) {
		return;
	}
	thecel->cd.cent.x = x;
	thecel->cd.cent.y = y;
	maybe_ref_flicel_pos(thecel);
}

/*****************************************************************************
 * void CelTurn(double angle);
 ****************************************************************************/
static void po_rotate_cel(double angle)
{
	if (!thecel) {
		return;
	}
	thecel->cd.rotang.z += angle * 1024 / 360;
	maybe_ref_flicel_pos(thecel);
}

/*****************************************************************************
 * void CelTurnTo(double angle);
 ****************************************************************************/
static void po_rotate_cel_to(double angle)
{
	if (!thecel) {
		return;
	}
	thecel->cd.rotang.z = angle * 1024 / 360;
	maybe_ref_flicel_pos(thecel);
}

/*****************************************************************************
 * if abspos < 0 then use relpos.  Return cel position
 ****************************************************************************/
static Errcode po_cel_seek(int abspos, int relpos)
{
	int err;
	int cpos;
	int npos;

	if (thecel != NULL) {
		err = reopen_fcelio(thecel, JREADONLY);
		if (err < Success) {
			goto OUT;
		}
		cpos = thecel->cd.cur_frame;
		if (abspos < 0) {
			npos = cpos + relpos;
		} else {
			npos = abspos;
		}
		if (npos != cpos) {
			err = seek_fcel_frame(thecel, npos);
		}
		if (err >= Success) {
			err = npos;
		}
		close_fcelio(thecel);
	} else {
		err = Err_not_found;
	}
OUT:
	return err;
}

/*****************************************************************************
 * bool CelExists(void);
 ****************************************************************************/
static bool po_cel_exists(void)
{
	return thecel != NULL;
}

/*****************************************************************************
 * ErrCode CelNextFrame(void);
 *****************************************************************************/
static Errcode po_cel_next_frame(void)
{
	return po_cel_seek(-1, 1);
}

/*****************************************************************************
 * ErrCode CelBackFrame(void);
 ****************************************************************************/
static Errcode po_cel_back_frame(void)
{
	return po_cel_seek(-1, -1);
}

/*****************************************************************************
 * int CelGetFrame(void);
 ****************************************************************************/
static Errcode po_cel_get_frame(void)
{
	return po_cel_seek(-1, 0);
}

/*****************************************************************************
 * ErrCode CelSetFrame(int frame);
 ****************************************************************************/
static Errcode po_cel_set_frame(int ix)
{
	return po_cel_seek(ix, 0);
}

/*****************************************************************************
 * int CelFrameCount(void);
 ****************************************************************************/
static int po_cel_frame_count(void)
{
	if (thecel == NULL) {
		return Err_not_found;
	}
	return thecel->flif.hdr.frame_count;
}

/*****************************************************************************
 * ErrCode CelWhere(int *x, int *y, double *angle);
 * returns x,y position of cel, and angle.
 ****************************************************************************/
static Errcode po_cel_where(Popot px, Popot py, Popot pangle)
{
	if (px.pt == NULL || py.pt == NULL || pangle.pt == NULL) {
		return builtin_err = Err_null_ref;
	}
	if (thecel == NULL) {
		return Err_not_found;
	}
	vass(px.pt, int) = thecel->cd.cent.x;
	vass(py.pt, int) = thecel->cd.cent.y;
	vass(pangle.pt, double) = thecel->cd.rotang.z * 360.0 / 1024.0;
	return Success;
}

/*****************************************************************************
 * ErrCode CelClip(void);
 *	 In addition to the types of errors the internal clip_cel() routine can
 *	 return to us (no memory, etc), we check to make sure that something
 *	 actually got clipped.	If thecel comes up NULL, it means that the
 *	 picscreen was totally blank; ie, there was nothing to clip.  We check
 *	 for this specifically because the internal clip_cel() routine doesn't.
 *	 (It would just leave Cel menu items disabled).  A program that gets
 *	 Success back from this function might reasonably assume that a cel then
 *	 exists and that other cel functions will work.
 ****************************************************************************/
static Errcode po_clip_cel(void)
{
	Errcode err = clip_cel();

	if (Success > err) {
		return err;
	}
	if (thecel == NULL) {
		return Err_not_found;
	}
	return Success;
}

/*****************************************************************************
 * ErrCode CelClipChanges(void);
 *	 In addition to the types of errors the internal clip_cel() routine can
 *	 return to us (no memory, etc), we check to make sure that something
 *	 actually got clipped.
 ****************************************************************************/
static Errcode po_clip_changes(void)
{
	qget_changes();
	if (thecel == NULL) {
		return Err_not_found;
	}
	return Success;
}

/*****************************************************************************
 * release thecel, if it exists.
 ****************************************************************************/
static void po_release_cel(void)
{
	if (thecel != NULL) {
		noask_delete_the_cel();
	}
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

PolibCel po_libcel = {
	po_cel_exists,      "Boolean CelExists(void);",
	po_ink_paste,       "void    CelPaste(void);",
	po_move_cel,        "void    CelMove(int dx, int dy);",
	po_move_cel_to,     "void    CelMoveTo(int x, int y);",
	po_rotate_cel,      "void    CelTurn(double angle);",
	po_rotate_cel_to,   "void    CelTurnTo(double angle);",
	po_cel_next_frame,  "ErrCode CelNextFrame(void);",
	po_cel_back_frame,  "ErrCode CelBackFrame(void);",
	po_cel_set_frame,   "ErrCode CelSetFrame(int frame);",
	po_cel_get_frame,   "int     CelGetFrame(void);",
	po_cel_frame_count, "int     CelFrameCount(void);",
	po_cel_where,       "ErrCode CelWhere(int *x, int *y, double *angle);",
	po_get_cel,         "ErrCode CelGet(int x, int y, int width, int height);",
	po_clip_cel,        "ErrCode CelClip(void);",
	po_release_cel,     "void    CelRelease(void);",
	po_clip_changes,    "ErrCode CelClipChanges(void);",
};

Poco_lib po_cel_lib = {
	NULL,
	"Cel",
	(const Lib_proto*)&po_libcel,
	POLIB_CEL_SIZE,
};
