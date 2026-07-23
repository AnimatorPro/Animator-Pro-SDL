/* pocouser.c - poco library functions to read input state and
   standard dialog boxes. */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "errcodes.h"
#include "linklist.h"
#include "jimk.h"
#include "poly.h"
#include "pocoface.h"
#include "pocolib.h"
#include "reqlib.h"
#include "softmenu.h"
#include "commonst.h"
#include "textedit.h"
#include "marqi.h"
#include "wildlist.h"
#include "scroller.h"
#include "poco_array.h"

extern bool hide_mouse(void);
extern bool show_mouse(void);
extern int qcolor();
extern Errcode builtin_err;
extern void disp_line_alot(Short_xy* v);
void cleanup_toptext();
Errcode po_poly_to_arrays(Poly* p, Popot* x, Popot* y);
Errcode po_arrays_to_poly(Poly* p, int ptcount, Popot* px, Popot* py);
extern int po_UdSlider(int* inum, int min, int max, void* update, void* data, char* fmt, ...);

extern void full_screen_edit(Text_file* gf);  // from qpocoed.c

/*****************************************************************************

/* abort handling... */

static struct {
	bool abortable;
	void* abort_handler;
	Popot abort_data;
} abort_control;

/*****************************************************************************
 * service routine (called from within qpoco.c)
 ****************************************************************************/
void po_init_abort_control(int abortable, void* handler)
{
	abort_control.abortable = abortable;
	abort_control.abort_handler = handler;
}

/*****************************************************************************
 * bool GetAbort(void)
 ****************************************************************************/
bool po_get_abortable(void)
{
	return abort_control.abortable;
}

/*****************************************************************************
 * bool SetAbort(bool abort)
 ****************************************************************************/
bool po_set_abortable(bool abort)
{
	bool was_abortable = abort_control.abortable;
	abort_control.abortable = abort;
	return was_abortable;
}

/*****************************************************************************
 * void SetAbortHandler(bool (*handler)(void *data), void *data)
 *
 * specify a poco routine to get called if the user aborts the poco program.
 *	 the specified routine is called immediately after the user selects YES
 *	 in pj's abort dialog box.  the abort handler is provided so that the
 *	 poco program can do any cleanup it thinks is necessary (close files, etc).
 *	 it has carte blanche to do pretty much whatever it wants, but bear in
 *	 mind that the user is expecting a pretty quick shutdown.  while the
 *	 abort routine is executing, further aborts are disabled automatically.
 *	 the routine could re-enable them, but nasty recursion issues must be
 *	 dealt with if it does.  the poco abort handler routine must return a
 *	 boolean value indicating whether pj should complete the abort processing
 *	 (true), or ignore it (false).	the only legitimate use of this is to
 *	 allow a deffered abort (the handler sets a flag telling itself to abort
 *	 on the next iteration of its main loop, or whatever).
 *
 *	 to un-install an abort handler, pass a NULL function pointer in the call.
 *
 *	 note that this routine only handles setting the vector; actual abort
 *	 handling is in po_check_abort, below.
 ****************************************************************************/
void po_set_abort_handler(void* abort_handler, void* abort_data)
{
	if (abort_handler == NULL) {
		abort_control.abort_handler = NULL;
		return;
	}

	if (NULL == (abort_control.abort_handler = po_fuf_code(abort_handler))) {
		builtin_err = Err_function_not_found;
		return;
	}

	Popot_make_null(&abort_control.abort_data);
	abort_control.abort_data.pt = abort_data;
	return;
}

/*****************************************************************************
 * handle abort checking and invokation of (optional) abort handler routine.
 *	see comments under po_set_abort_handler, above.
 ****************************************************************************/
bool po_check_abort(void* nobody)
{
	bool mouse_was_on;
	Errcode err;
	Pt_num retval;
	PocoCallbackValue callback_args[1];

	if (abort_control.abortable) {
		if (poll_abort() < Success) {
			mouse_was_on = show_mouse();
			if (soft_yes_no_box("poco_abort")) {
				if (NULL == abort_control.abort_handler) {
					return true;
				} else {
					abort_control.abortable = false; /* prevent bad recursion */
					callback_args[0].kind = POCO_CALLBACK_VALUE_POPOT;
					callback_args[0].value.popot_value = abort_control.abort_data;
					err = poco_invoke_callback(abort_control.abort_handler, &retval, callback_args,
											   1);
					if (err < Success) {
						return builtin_err = err;
					}
					if (retval.i != false) {
						return true;
					}
					abort_control.abortable = true;
				}
			}
			if (!mouse_was_on) {
				hide_mouse();
			}
		}
	}
	return false;
}

/*****************************************************************************
 * service routine — validate 5 input pointers
 ****************************************************************************/
static bool check_input_ptrs(int* x, int* y, int* left, int* right, int* key)
{
	if (x == NULL || y == NULL || left == NULL || right == NULL || key == NULL) {
		builtin_err = Err_null_ref;
		return false;
	}
	return true;
}

/*****************************************************************************
 * service routine — fill in input values from icb state
 ****************************************************************************/
static void set_input_values(int* x, int* y, int* left, int* right, int* key)
{
	*x = icb.mx;
	*y = icb.my;
	*left = ISDOWN(MBPEN);
	*right = ISDOWN(MBRIGHT);
	if (JSTHIT(KEYHIT)) {
		*key = icb.inkey;
	} else {
		*key = 0;
	}
}

/*****************************************************************************
 * Get input from pen-window.
 ****************************************************************************/
static void po_wndo_input(int* x, int* y, int* left, int* right, int* key, ULONG flags)
{
	if (check_input_ptrs(x, y, left, right, key)) {
		wait_wndo_input(flags);
		set_input_values(x, y, left, right, key);
	}
}

/*****************************************************************************
 * void WaitClick(int *x, int *y, int *left, int *right, int *key)
 ****************************************************************************/
static void po_wait_click(int* x, int* y, int* left, int* right, int* key)
{
	po_wndo_input(x, y, left, right, key, ANY_CLICK);
}

/*****************************************************************************
 * void PollInput(int *x, int *y, int *left, int *right, int *key)
 ****************************************************************************/
static void po_poll_input(int* x, int* y, int* left, int* right, int* key)
{
	po_wndo_input(x, y, left, right, key, ANY_INPUT);
}

/*****************************************************************************
 * void WaitInput(int *x, int *y, int *left, int *right, int *key)
 ****************************************************************************/
static void po_wait_input(int* x, int* y, int* left, int* right, int* key)
{
	po_wndo_input(x, y, left, right, key, MMOVE | ANY_CLICK);
}

/*****************************************************************************
 * Get input from physical screen.
 ****************************************************************************/
static void po_physical_input(int* x, int* y, int* left, int* right, int* key, ULONG flags)
{
	if (check_input_ptrs(x, y, left, right, key)) {
		wait_input(flags);
		set_input_values(x, y, left, right, key);
	}
}

/*****************************************************************************
 * void PhysicalWaitClick(int *x, int *y, int *left, int *right, int *key)
 ****************************************************************************/
static void po_physical_wait_click(int* x, int* y, int* left, int* right, int* key)
{
	po_physical_input(x, y, left, right, key, ANY_CLICK);
}

/*****************************************************************************
 * void PhysicalPollInput(int *x, int *y, int *left, int *right, int *key)
 ****************************************************************************/
static void po_physical_poll_input(int* x, int* y, int* left, int* right, int* key)
{
	if (check_input_ptrs(x, y, left, right, key)) {
		check_input(ANY_INPUT);
		set_input_values(x, y, left, right, key);
	}
}

/*****************************************************************************
 * void PhysicalWaitInput(int *x, int *y, int *left, int *right, int *key)
 ****************************************************************************/
static void po_physical_wait_input(int* x, int* y, int* left, int* right, int* key)
{
	po_physical_input(x, y, left, right, key, MMOVE | ANY_CLICK);
}

/*****************************************************************************
 * bool PhysicalRubMoveBox(int *x, int *y, int *w, int *h
 , bool clip_to_screen)
 ****************************************************************************/
static bool po_physical_rub_move_box(int* x, int* y, int* w, int* h, bool clip_to_screen)
{
	Wscreen* ws = icb.input_screen;
	Marqihdr md;
	Rectangle rect;
	Rectangle clip_rect;
	Rectangle* pclip_rect = NULL;

	if (x == NULL || y == NULL || w == NULL || h == NULL) {
		builtin_err = Err_null_ref;
		return (false);
	}
	rect.x = *x;
	rect.y = *y;
	rect.width = *w;
	rect.height = *h;
	if (clip_to_screen) {
		copy_rectfields(ws->viscel, &clip_rect);
		pclip_rect = &clip_rect;
	}
	init_marqihdr(&md, ws->viscel, NULL, ws->SWHITE, ws->SBLACK);
	if (marqmove_rect(&md, &rect, pclip_rect) < 0) {
		return false;
	}
	*x = rect.x;
	*y = rect.y;
	*w = rect.width;
	*h = rect.height;
	return true;
}

/* Poco 'marqi' routines to define geometric shapes */

/*****************************************************************************
 * bool RubBox(int *x, int *y, int *w, int *h)
 ****************************************************************************/
static bool po_rub_box(int* x, int* y, int* w, int* h)
{
	Rectangle rect;

	if (x == NULL || y == NULL || w == NULL || h == NULL) {
		builtin_err = Err_null_ref;
		return (false);
	}
	if (cut_out_rect(&rect) < 0) {
		return (false);
	} else {
		*x = rect.x;
		*y = rect.y;
		*w = rect.width;
		*h = rect.height;
		return (true);
	}
}

/*****************************************************************************
 * bool RubLine(int x1, int y1, int *x2, int *y2)
 ****************************************************************************/
static bool po_rub_line(int x1, int y1, int* x2, int* y2)
{
	Short_xy xys[2];
	int ret;

	if (x2 == NULL || y2 == NULL) {
		builtin_err = Err_null_ref;
		return (false);
	}
	xys[0].x = x1;
	xys[0].y = y1;
	ret = rubba_vertex(&xys[0], &xys[1], &xys[0], disp_line_alot, vs.ccolor);
	cleanup_toptext();
	if (ret < 0) {
		return (false);
	} else {
		*x2 = xys[1].x;
		*y2 = xys[1].y;
		return (true);
	}
}

/*****************************************************************************
 * bool RubCircle(int *x, int *y, int *rad)
 ****************************************************************************/
static bool po_rub_circle(int* x, int* y, int* rad)
{
	Circle_p circp;

	if (x == NULL || y == NULL || rad == NULL) {
		builtin_err = Err_null_ref;
		return (false);
	}
	wait_wndo_input(ANY_CLICK);
	if (!ISDOWN(MBPEN)) {
		return (false);
	}
	if (get_rub_circle(&circp.center, &circp.diam, vs.ccolor) < 0) {
		return (false);
	}
	*x = circp.center.x;
	*y = circp.center.y;
	*rad = circp.diam >> 1;
	return (true);
}

/*****************************************************************************
 * int RubPoly(int **x, int **y)
 * returns # of points or negative Errcode
 ****************************************************************************/
static int po_rub_poly(int** px, int** py)
{
	Poly p;
	Errcode err;
	LLpoint* pt;
	int *xarr, *yarr;
	int i;
	Popot ppt;

	if (px == NULL || py == NULL) {
		return (builtin_err = Err_null_ref);
	}
	wait_wndo_input(ANY_CLICK);
	if (!ISDOWN(MBPEN)) {
		return (0);
	}
	clear_struct(&p);
	make_poly(&p, vs.closed_curve);
	if (p.pt_count <= 0) {
		free_polypoints(&p);
		*px = NULL;
		*py = NULL;
		return 0;
	}

	/* Allocate arrays for x and y coordinates */
	ppt = poco_lmalloc(p.pt_count * sizeof(int));
	if (ppt.pt == NULL) {
		free_polypoints(&p);
		return Err_no_memory;
	}
	xarr = ppt.pt;

	ppt = poco_lmalloc(p.pt_count * sizeof(int));
	if (ppt.pt == NULL) {
		free_polypoints(&p);
		return Err_no_memory;
	}
	yarr = ppt.pt;

	/* Copy points to arrays */
	for (i = 0, pt = p.clipped_list; pt != NULL && i < p.pt_count; pt = pt->next, i++) {
		xarr[i] = pt->x;
		yarr[i] = pt->y;
	}

	*px = xarr;
	*py = yarr;
	err = p.pt_count;
	free_polypoints(&p);
	return (err);
}

/*****************************************************************************
 * bool DragBox(int *x, int *y, int *w, int *h)
 ****************************************************************************/
static bool po_drag_box(int* x, int* y, int* w, int* h)
{
	Rectangle rect;

	if (x == NULL || y == NULL || w == NULL || h == NULL) {
		builtin_err = Err_null_ref;
		return (false);
	}

	rect.x = *x;
	rect.y = *y;
	rect.width = *w;
	rect.height = *h;

	if (rect_in_place(&rect) >= Success) {
		if (clip_move_rect(&rect) >= Success) {
			*x = rect.x;
			*y = rect.y;
			*w = rect.width;
			*h = rect.height;
			return true;
		}
	}

	return false;
}

/*****************************************************************************
 * int printf(char *format, ...)
 ****************************************************************************/
static int po_ttextf(char* fmt, ...)
{
	va_list args;
	int rv;

	va_start(args, fmt);
	if (fmt == NULL) {
		fmt = "";
	}
	rv = ttextf(fmt, args, NULL);
	va_end(args);
	return rv;
}

/*****************************************************************************
 * ErrCode Qerror(ErrCode err, char *format, ...)
 ****************************************************************************/
static Errcode po_ErrBox(Errcode err, char* fmt, ...)
{
	char etext[ERRTEXT_SIZE];
	va_list args;
	bool mouse_was_on;

	if (!get_errtext(err, etext)) {
		return (err);
	}

	va_start(args, fmt);
	if (fmt == NULL) {
		fmt = "";
	}

	mouse_was_on = show_mouse();
	varg_continu_box(NULL, fmt, args, etext);
	if (!mouse_was_on) {
		hide_mouse();
	}
	va_end(args);
	return (Err_reported);
}

/*****************************************************************************
 * void Qtext(char *format, ...)
 ****************************************************************************/
static void po_TextBox(char* fmt, ...)
{
	va_list args;
	bool mouse_was_on;

	va_start(args, fmt);
	if (fmt == NULL) {
		fmt = "";
	}
	mouse_was_on = show_mouse();
	varg_continu_box(NULL, fmt, args, NULL);
	if (!mouse_was_on) {
		hide_mouse();
	}
	va_end(args);
}

/*****************************************************************************
 * Boolean Qquestion(char *question, ...)
 ****************************************************************************/
static int po_YesNo(char* question, ...)
{
	va_list args;
	bool rv;
	bool mouse_was_on;

	va_start(args, question);
	if (question == NULL) {
		question = "";
	}
	mouse_was_on = show_mouse();
	rv = varg_yes_no_box(NULL, question, args);
	if (!mouse_was_on) {
		hide_mouse();
	}
	va_end(args);
	return rv;
}

/*****************************************************************************
 * bool Qnumber(int *num, int min, int max, char *header)
 ****************************************************************************/
static bool po_Slider(int* inum, int min, int max, char* hailing)
{
	short num;
	bool cancel;
	bool mouse_was_on;

	if (hailing == NULL) {
		hailing = "";
	}

	if (inum == NULL) {
		return (builtin_err = Err_null_ref);
	}
	num = *inum;

	if ((num < SHRT_MIN) || (min < SHRT_MIN) || (max < SHRT_MIN) || (num > SHRT_MAX) ||
		(min > SHRT_MAX) || (max > SHRT_MAX) || (min > max)) {
		return (builtin_err = Err_parameter_range);
	}

	mouse_was_on = show_mouse();
	if (false != (cancel = qreq_number(&num, min, max, "%s", hailing))) {
		*inum = num;
	}
	if (!mouse_was_on) {
		hide_mouse();
	}
	return cancel;
}

/* Decode one element of a poco char* array; see poco_array.h for the why. */
char* po_array_str(char** poco_array, int index)
{
	return (char*)((Popot*)poco_array)[index].pt;
}

/*****************************************************************************
 * int Qchoice(char **buttons, int bcount, char *header, ...)
 ****************************************************************************/
static Errcode po_ChoiceBox(char** pchoices, int ccount, char* fmt, ...)
{
	va_list args;
	char* choices[TBOX_MAXCHOICES + 1];
	int i;
	Errcode rv;
	bool mouse_was_on;

	/* do some error checking on parameters */
	if (ccount > TBOX_MAXCHOICES) {
		ccount = TBOX_MAXCHOICES;
	}

	/* Transfer button strings into NULL-terminated list of C pointers */
	if (pchoices == NULL) {
		return (builtin_err = Err_null_ref);
	}
	for (i = 0; i < ccount; ++i) {
		if (NULL == (choices[i] = po_array_str(pchoices, i))) {
			return (builtin_err = Err_null_ref);
		}
	}
	choices[ccount] = NULL;

	va_start(args, fmt);
	if (fmt == NULL) {
		fmt = "";
	}
	mouse_was_on = show_mouse();
	rv = tboxf_choice(icb.input_screen, NULL, fmt, args, choices, NULL);
	if (!mouse_was_on) {
		hide_mouse();
	}
	va_end(args);
	return rv;
}

/*****************************************************************************
 * bool Qfile(char *suffix, char *button,
 *	  char *inpath, char *outpath, bool force_suffix, char *header)
 ****************************************************************************/
static bool po_FileMenu(char* suffix, char* button, char* inpath, char* outpath, int force_suffix,
						char* prompt)
{
	bool rv = true;
	bool mouse_was_on;
	char titbuf[40];

	if (prompt == NULL || 0 == strlen(prompt)) {
		prompt = stack_string("poco_qfile", titbuf);
	}

	if (suffix == NULL || 0 == strlen(suffix) || '.' != *suffix) {
		force_suffix = false;
		suffix = ".*";
	}

	if (button == NULL || 0 == strlen(button)) {
		button = ok_str;
	}

	if (inpath == NULL || outpath == NULL) {
		builtin_err = Err_null_ref;
		return false;
	}

	mouse_was_on = show_mouse();
	if (NULL ==
		pj_get_filename(prompt, suffix, button, inpath, outpath, force_suffix, NULL, NULL)) {
		rv = false;
	}
	if (!mouse_was_on) {
		hide_mouse();
	}

	return rv;
}

/*****************************************************************************
 * bool Qstring(char *string, int size, char *header)
 ****************************************************************************/
static bool po_qstring(char* strbuf, int bufsize, char* hailing)
{
	bool rv;
	bool mouse_was_on;

	if (hailing == NULL) {
		hailing = "";
	}
	if (strbuf == NULL) {
		builtin_err = Err_null_ref;
		return false;
	}
	if (bufsize < 2) {
		builtin_err = Err_buf_too_small;
		return (false);
	}
	mouse_was_on = show_mouse();
	rv = qreq_string(strbuf, bufsize - 1, "%s", hailing);
	if (!mouse_was_on) {
		hide_mouse();
	}
	return rv;
}

static int po_some_choice(char** pchoices, int ccount, USHORT* flags, char* header)
{
#define CMAX 10
#define CHMAX 60
	char* pbuf[CMAX];
	int i;
	bool mouse_was_on;

	if (ccount < 0 || ccount > CMAX) {
		return (builtin_err = Err_parameter_range);
	}
	if (header == NULL) {
		header = "";
	}
	if (pchoices == NULL) {
		return (builtin_err = Err_null_ref);
	}
	for (i = 0; i < ccount; ++i) {
		if (NULL == (pbuf[i] = po_array_str(pchoices, i))) {
			return (builtin_err = Err_null_ref);
		}
	}
	for (i = 0; i < ccount; i++) {
		if (strlen(pbuf[i]) > CHMAX) {
			return (builtin_err = Err_string);
		}
	}
	mouse_was_on = show_mouse();
	i = qchoice(flags, header, pbuf, ccount);
	if (!mouse_was_on) {
		hide_mouse();
	}
	if (i < 0) {
		return (0);
	}
	return (i + 1);
#undef CHMAX
#undef CMAX
}

/*****************************************************************************
 * int Qmenu(char **choices, int ccount, char *header)
 ****************************************************************************/
static int po_qmenu(char** pchoices, int ccount, char* header)
{
	return po_some_choice(pchoices, ccount, NULL, header);
}

/*****************************************************************************
 *int  QmenuWithFlags(char **choices, int ccount, short *flags, char *header)
 ****************************************************************************/
static int po_qmenu_with_flags(char** pchoices, int ccount, short* pflags, char* header)
{
	int i;
	USHORT save;
	USHORT* flags = (USHORT*)pflags;
	if (flags != NULL) {
		/* Rotate flags around to make indexes match the return choice value.
		 * That is put "cancel" at zero. */
		save = flags[0];
		for (i = 1; i < ccount; ++i) {
			flags[i - 1] = flags[i];
		}
		flags[ccount - 1] = save;
	}
	return po_some_choice(pchoices, ccount, flags, header);
}

/*****************************************************************************
 * convert a poco char *names[]  array to a Names list
 ****************************************************************************/
static Errcode strarr_to_names(char** pp, int pcount, Names** pnames)
{
	void* s;
	int i;
	Errcode err;
	Names* names = NULL;
	Names* new;

	if (pp == NULL) {
		err = Err_null_ref;
		goto ERR;
	}
	for (i = 0; i < pcount; i++) {
		if ((s = po_array_str(pp, i)) == NULL) {
			err = Err_null_ref;
			goto ERR;
		}
		if ((new = pj_malloc(sizeof(*new))) == NULL) {
			err = Err_no_memory;
			goto ERR;
		}
		new->name = s;
		new->next = names;
		names = new;
	}
	*pnames = reverse_slist(names);
	return (Success);
ERR:
	free_wild_list(&names);
	*pnames = NULL; /* initialize return list at empty */
	return (err);
}

/*****************************************************************************
 *
 * bool Qlist(char *choicestr, int *choice,
 *	 char **items, int icount, int *ipos, char *header)
 ****************************************************************************/
static bool po_Qlist(char* choice_str, int* choice_ix, char** items, int icount, int* ipos,
					 char* header)
{
	Names* nlist = NULL;
	Names* nsel;
	char* retbuf = NULL;
	bool retbuf_allocated = false;
	short ipo = 0;
	bool ret = false;
	int maxchars;
	bool mouse_was_on;

	if (choice_ix == NULL) {
		builtin_err = Err_null_ref;
		goto OUT;
	}
	if (ipos != NULL) {
		ipo = *ipos;
	}
	if (header == NULL) {
		header = "";
	}
	if ((builtin_err = strarr_to_names(items, icount, &nlist)) < Success) {
		goto OUT;
	}
	maxchars = longest_name(nlist) + 1;

	if (choice_str == NULL) {
		if ((retbuf = pj_zalloc(maxchars)) == NULL) {
			builtin_err = Err_no_memory;
			goto OUT;
		}
		retbuf_allocated = true;
	} else {
		retbuf = choice_str;
	}

	mouse_was_on = show_mouse();
	if ((ret = qscroller(retbuf, header, nlist, 10, &ipo)) != 0) {
		nsel = name_in_list(retbuf, nlist);
		*choice_ix = slist_ix(nlist, nsel);
	}
	if (!mouse_was_on) {
		hide_mouse();
	}
	if (ipos != NULL) {
		*ipos = ipo;
	}
OUT:
	free_wild_list(&nlist);
	if (retbuf_allocated) {
		pj_gentle_free(retbuf);
	}
	return (ret);
}

static int lastbtn;
static Names* lastsel;

static Errcode remember_ok_btn(Names* which, void* data)
{
	lastsel = which;
	lastbtn = 1;
	return Success;
}

static bool remember_info_btn(Names* which, void* data)
{
	lastsel = which;
	lastbtn = 2;
	return true;
}

/*****************************************************************************
 * bool Qscroll(int *choice, char **items, int icount, int *ipos, char *hdr)
 ****************************************************************************/
static int po_Qscroll(int* choice_ix, char** items, int icount, int* ipos, char** button_texts,
					  char* header)
{
	Names* nlist = NULL;
	Names* cursel;
	char* btexts[3];
	char** usebtexts;
	short ipo = -1;
	int ret;
	int maxchars;
	bool mouse_was_on;
	void* use_info_btn;  // lazy, lazy

	if (choice_ix == NULL) {
		builtin_err = Err_null_ref;
		goto OUT;
	}

	if (header == NULL) {
		header = "";
	}

	if ((builtin_err = strarr_to_names(items, icount, &nlist)) < Success) {
		goto OUT;
	}
	maxchars = longest_name(nlist) + 1;

	if (ipos != NULL) {
		ipo = *ipos;
	}
	if (ipo < 0) {
		cursel = NULL;
	} else {
		cursel = slist_el(nlist, ipo);
	}

	if (button_texts == NULL) {
		usebtexts = NULL;
	} else {
		usebtexts = btexts;
		if ((NULL == (btexts[0] = po_array_str(button_texts, 0))) ||
			(NULL == (btexts[2] = po_array_str(button_texts, 2)))) {
			builtin_err = Err_null_ref;
			goto OUT;
		}
		if (NULL == (btexts[1] = po_array_str(button_texts, 1))) {
			btexts[1] = "";
		}
	}

	if (*btexts[1] == '\0') {
		use_info_btn = NULL;
	} else {
		use_info_btn = remember_info_btn;
	}

	lastbtn = 0;
	lastsel = NULL;
	mouse_was_on = show_mouse();
	ret = go_driver_scroller(header, nlist, cursel, remember_ok_btn, use_info_btn, NULL, usebtexts);
	if (!mouse_was_on) {
		hide_mouse();
	}

	*choice_ix = ipo = slist_ix(nlist, lastsel);

	if (ret >= Success) {
		ret = lastbtn;
	}

	if (ipos != NULL) {
		*ipos = ipo;
	}
OUT:
	free_wild_list(&nlist);
	return (ret);
}

/*****************************************************************************
 * Check that the cursor-position and top-line-in-text-window pointers are
 * good,  and call the text editor.  Returns error or size of text.
 ****************************************************************************/
static int position_cursor_and_edit(Text_file* gf, int* cursor_position, int* top_line)
{
	static int stop_line, scursor_position;
	int* pcursor_position;
	int* ptop_line;

	/* Set cursor & top line in window position to be the what they pass in
	 * or if they pass in NULL just whatever it last was. */
	pcursor_position = (cursor_position != NULL) ? cursor_position : &scursor_position;
	ptop_line = (top_line != NULL) ? top_line : &stop_line;

	gf->text_yoff = *ptop_line;
	gf->tcursor_p = *pcursor_position;
	full_screen_edit(gf);
	*pcursor_position = gf->tcursor_p;
	*ptop_line = gf->text_yoff;
	return gf->text_size;
}

/*****************************************************************************
 * Edit a text buffer in memory.  Input should be a zero-terminated string
 * inside a buffer of at least max_size.  (The max size should be two
 * characters longer than the string you actually want.  For instance
 * if they need to edit an 8 character file name make max_size 10.)
 * It's ok for cursor_position and top_line to be NULL.
 ****************************************************************************/
static int po_edit(char* text, int max_size, int* cursor_position, int* top_line)
{
	Text_file tf;

	if (text == NULL) {
		return (builtin_err = Err_null_ref);
	}
	clear_struct(&tf);
	tf.text_alloc = max_size;
	tf.text_buf = text;
	tf.text_size = strlen(tf.text_buf);
	return position_cursor_and_edit(&tf, cursor_position, top_line);
}

/*****************************************************************************
 * int QeditFile(char *file_name, int *cursor_position, int *top_line);
 *		Read in a file, edit it, and write it back out.
 ****************************************************************************/
static int po_edit_file(char* file_name, int* cursor_position, int* top_line)
{
	Text_file tf;
	int size;
	Errcode err;

	if (file_name == NULL) {
		return (builtin_err = Err_null_ref);
	}

	clear_struct(&tf);
	load_text_file(&tf, file_name);
	size = position_cursor_and_edit(&tf, cursor_position, top_line);
	if ((err = save_text_file(&tf)) < Success) {
		size = err;
	}
	free_text_file(&tf);
	return size;
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

PolibUser po_libuser = {
	po_ttextf,
	"int     printf(char *format, ...);",
	cleanup_toptext,
	"void    unprintf(void);",
	po_TextBox,
	"void    Qtext(char *format, ...);",
	po_ChoiceBox,
	"int     Qchoice(char **buttons, int bcount, char *header, ...);",
	po_qmenu,
	"int     Qmenu(char **choices, int ccount, char *header);",
	po_YesNo,
	"Boolean Qquestion(char *question, ...);",
	po_Slider,
	"Boolean Qnumber(int *num, int min, int max, char *header);",
	po_qstring,
	"Boolean Qstring(char *string, int size, char *header);",
	po_FileMenu,
	"Boolean Qfile(char *suffix, char *button,"
	" char *inpath, char *outpath, Boolean force_suffix, char *header);",
	po_Qlist,
	"Boolean Qlist(char *choicestr, int *choice, "
	"char **items, int icount, int *ipos, char *header);",
	qcolor,
	"int     Qcolor(void);",
	po_ErrBox,
	"ErrCode Qerror(ErrCode err, char *format, ...);",
	po_rub_box,
	"Boolean RubBox(int *x, int *y, int *w, int *h);",
	po_rub_circle,
	"Boolean RubCircle(int *x, int *y, int *rad);",
	po_rub_line,
	"Boolean RubLine(int x1, int y1, int *x2, int *y2);",
	po_rub_poly,
	"int     RubPoly(int **x, int **y);",
	po_drag_box,
	"Boolean DragBox(int *x, int *y, int *w, int *h);",
	po_wait_click,
	"void    WaitClick(int *x, int *y, int *left, int *right, int *key);",
	po_poll_input,
	"void    PollInput(int *x, int *y, int *left, int *right, int *key);",
	po_wait_input,
	"void    WaitInput(int *x, int *y, int *left, int *right, int *key);",
	po_get_abortable,
	"Boolean GetAbort(void);",
	po_set_abortable,
	"Boolean SetAbort(Boolean abort);",
	po_set_abort_handler,
	"void    SetAbortHandler(Boolean (*handler)(void *data), void *data);",
	hide_mouse,
	"Boolean HideCursor(void);",
	show_mouse,
	"Boolean ShowCursor(void);",
	po_Qscroll,
	"Boolean Qscroll(int *choice, char **items, int icount,"
	" int *ipos, char **button_texts, char *header);",
	po_UdSlider,
	"Boolean UdQnumber(int *num, int min, int max,"
	"Errcode (*update)(void *data, int num), void *data, char *fmt,...);",
	po_edit,
	"int Qedit(char *text_buffer, int max_size,  int *cursor_position,"
	" int *top_line);",
	po_edit_file,
	"int QeditFile(char *file_name, int *cursor_position, int *top_line);",
	po_physical_wait_click,
	"void PhysicalWaitClick(int *x, int *y, int *left, int *right, int *key);",
	po_physical_poll_input,
	"void PhysicalPollInput(int *x, int *y, int *left, int *right, int *key);",
	po_physical_wait_input,
	"void PhysicalWaitInput(int *x, int *y, int *left, int *right, int *key);",
	po_physical_rub_move_box,
	"Boolean PhysicalRubMoveBox(int *x, int *y, int *width, int *height, Boolean clip_to_screen);",
	po_qmenu_with_flags,
	"int     QmenuWithFlags(char **choices, int ccount"
	", short *flags, char *header);",
};

Poco_lib po_user_lib = {
	NULL,
	"User Interface",
	(Lib_proto*)&po_libuser,
	POLIB_USER_SIZE,
};
