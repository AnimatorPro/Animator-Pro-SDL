#ifndef MENUS_H
#define MENUS_H

#ifndef STDTYPES_H
	#include "stdtypes.h"
#endif

#ifndef WNDO_H
	#include "wndo.h"
#endif

#ifndef RASTEXT_H
	#include "rastext.h"
#endif

#ifndef GFX_H
	#include "gfx.h"
#endif

typedef struct button Button;
typedef void (*BFUNC)(Button* b);    /* pointer to button callback function */

typedef struct button 
{
	struct button *next;		/* pointer to sibling buttons */
	struct button *children;  	/* sub buttons */
	Rectangle orig_rect;		/* Resolution independent coors */
	void *datme;				/* data for seeme and feelmes */
	BFUNC seeme;                /* function to draw button */
	BFUNC feelme;               /* left mouse click func (pen down) */
	void *optme;                /* sometimes right click func sometimes data */
	void *group;				/* a group of radio buttons share this */
	SHORT identity;				/* which one of radio button this is */
	SHORT key_equiv;			/* keyboard equivalent */
	USHORT flags;				/* little bits o'stuff see below */
	struct menuwndo *root;		/* pointer to root menu window or menu header
								 * allways a Menuwndo when called in a seeme */
	RECT_FIELDS;				/* scaled width,height,x,y  in that order */
} Button;

/* button flags values */

/* button highliting and *group types */

#define MB_NOHILITE		0       /* for documentation only */
#define MB_GHILITE		1	    /* button is hilit if *(SHORT*)group 
								 * is == b->identity */
#define MB_B_GHILITE 	2		/* button is hilit if *(BYTE*)group 
								 * is == (BYTE)b->identity */

#define MB_HILITE_TYPES 0x0007
/* 3 to 7 are as of yet unused */

#define MB_HILIT		0x0008	/* (flag) button MUST be rendered hilit */

/* end of hilite flags */

#define MB_OPTISDATA  	0x0010	/* the optme is data and not a function */
#define MB_ROOTISWNDO	0x0020	/* the root is pointer to the menuwndo
								 * else it points to the menuheader or NULL */
#define MB_DISABLED		0x0040  /* disabled flag */
#define MB_GHANG		0x0080  /* when hanging children hang root group too */
#define MB_DISABOPT		0x0100  /* optme is also disabled when MB_DISABLED is
								 * true */
#define MB_HIONSEL		0x0200  /* hilight when selected until penup */
#define MB_ASTERISK		0x0400  /* text get's an asterisk */
#define MB_NORESCALE    0x0800  /* No menu scaling on this button */
#define MB_SCALE_ABSW   0x1000  /* when scaling, scale the width to the 
								 * absolute value of it's position and NOT 
								 * relative to the button's start */
#define MB_SCALE_ABSH   0x2000  /* when scaling, scale the height to the 
								 * absolute value of it's position and NOT 
								 * relative to the button's start */
#define MB_SCALE_ABS (MB_SCALE_ABSW|MB_SCALE_ABSH) /* both */

/* macros to staticly init Button */

/* this will init button from old version 0 declarations 
 * sets flags to default MB_GHILITE type */

/* current version 1 button initialization with flags init */

#define MB_INIT1(n,c,w,h,x,y,dm,sm,fm,om,g,id,ke,fl) \
 {n,c,{w,h,x,y},dm,sm,fm,om,g,id,ke,fl,NULL,w,h,x,y}

/* no action field initializers */

#define NONEXT  ((Button *)NULL)
#define NOCHILD ((Button *)NULL)
#define NOTEXT	((char *)NULL)
#define NOSEE	((VFUNC)NULL)
#define NOFEEL	((VFUNC)NULL) 
#define NOGROUP	NULL
#define NOKEY	0
#define NOOPT	NULL
#define NODATA  NULL


void scale_rect(Rscale *scale, Rectangle *in, Rectangle *out);
void scale_xlist(Rscale *scale, SHORT *in, SHORT *out, int dim);
void scale_ylist(Rscale *scale, SHORT *in, SHORT *out, int dim);
void scale_xylist(Rscale *scale, Short_xy *in, Short_xy *out, int dim);

#define rscale_x(r,x) ((SHORT)((long)(x)*((r)->xscalep)/((r)->xscaleq)))
#define rscale_y(r,y) ((SHORT)((long)(y)*((r)->yscalep)/((r)->yscaleq)))

Rscale *mb_get_menu_scale(Button *b); /* gets scale for button's screen */
int mb_mscale_x(Button *b,int x);
int mb_mscale_y(Button *b,int y);

typedef struct menuhdr {
	Rectangle orig_rect;		/* Resolution independent coors */
	SHORT wndoid;		/* id for menu window loaded into menu->w.userid */
	SHORT type;			/* pull or panel etc what kind of mbs buttons */
	void *mbs;  	/* button or pull list */
	struct vfont *font;  /* font for buttons sliders etc if this is NULL
							 * font in Menuwndo is set to screen font */
	Cursorhdr *cursor; /* the cursor for this menu */

	void (*seebg)(struct menuwndo *m); /* the background seeme
									* if this is present it is responsible for
									* covering all of the menu since garbage
									* will be in it. if it is not present
									* menu window will be a white box */

	void *dodata;		/* just data for functions, used for the "selit"
						 * domenu function of a pullmenu. unused by panelmenu 
						 * iterpreter and can have other data */

	int (*domenu)(struct menuhdr *m); /* function to call for input in ioflags
									   * to return non zero if ate key input
									   * 0 if not */


	USHORT ioflags;		/* flags for what io is to be directed to domenu() */


	USHORT flags;       /* general purpose control flags */


/* fields never initialized but filled in by open menu */
	struct menuwndo *mw; /* pointer to open menuwndo for this menu */

	Mugroup *group; /* group node attached to */
	Dlnode node;	/* node for attachment to group menuhdr list */
	procmouse_func procmouse; /* mouse processor function */
	void (*on_showhide)(struct menuhdr *m, bool showing);
						/* function to call when menu being shown or hidden */
	void (*cleanup)(struct menuhdr *mh); /* called on close_menu() */
	RECT_FIELDS;	/* scaled width, height, x, y in that order */
} Menuhdr;

#define MENU_INIT0(w,h,x,y,id,typ,but,fnt,cur,see,dat,domu,io,fl,pmou,osh,clu)\
 {{w,h,x,y},id,typ,but,fnt,cur,see,dat,domu,io,fl,\
 NULL,NULL,{NULL,NULL},pmou,osh,clu,w,h,x,y}


/* flags values initialized by user */

#define MENU_KEYSONHIDE	0x0001 /* always process keys even if menu is hidden */
#define MENU_NOBCLIP	0x0002 /* prevents bclip on open */
#define MENU_DISABLED	0x0004 /* for user use */
#define MENU_NORESCALE  0x0008 /* scaling disabled for this menu */
#define MENU_NOMB_RESCALE  0x0010 /* button scaling disabled for this menu 
								   * does not inhibit button scaling via
								   * hang children */

/* flags only set by open menu: */

		/* Menuhdr->domenu was NULL and is currently default setting */
#define DOMENU_DEFAULT 0x0100  

/* some field initializers for Menuhdrs */

#define SCREEN_FONT (NULL)
#define SCREEN_CURSOR ((struct cursorhdr *)NULL)

/* menu "mbs" types */

#define PANELMENU	0
#define PULLMENU	1

typedef struct menuwndo {
	Wndo w;   		/* the menu's display window */
	struct vfont *font;
	Menuhdr *hdr; 	/* back pointer to header */
} Menuwndo;

/*** abort key checker ***/

extern SHORT menu_abortkeys[2];

extern int button_keyhit(Menuhdr *mh, Button *mbs, VFUNC prehit);
extern bool is_abortkey(void); /* checks icb.inkey against abortkeys */
extern int check_reqabort(Menuhdr *hdr);

extern Button *hit_button(Button *b, SHORT x, SHORT y);

/* data refers to data in qslider */

typedef struct qslfmt {
	void (*format)(void *val,char *buff,void *data);
	void (*decinc)(void *val, bool going_up,void *data);
	void (*scale_val)(void *val,USHORT scale,void *data);
	USHORT (*get_scale)(void *val,void *data);
} Qslfmt;

#define QSL_SCALEMAX 0x7FFF

typedef struct qslider {
	SHORT min, max;     /* only used for some formats */
	void *value;
	Image **arrows;     /* left[0] and right[1] arrow imagery 
										(will not draw if NULL) */
	SHORT arrwidth;		/* width of arrow boxes if 0 no boxes */
	SHORT flags;
	void (*update)(void *data,Button *b);	/* called with data pointer 
											 * and root button */
	Qslfmt *fmt; /* optional extended format */
	void *data;
} Qslider;

int clip_to_slider(int val, Qslider *qs);

/* flags */

#define QSL_LARRBOX		0x0001   /* draw box around left arrow */
#define QSL_RARRBOX		0x0002   /* draw box around right arrow */
#define QSL_MAXWRAP		0x0004	 /* wrap back to min if greater than max */
#define QSL_MINWRAP		0x0008	 /* wrap back to max if greater than max */


#define QSL_LARROW 0 /* defines for arrow indices in arrows */
#define QSL_RARROW 1

extern Qslfmt qsfmt_short, qsfmt_shortp1; /* p1 adds one to displayed number */

#define QSL_INIT1(mi,ma,v,add1,ud,aro) \
 {mi,ma,(v),aro,12,QSL_LARRBOX|QSL_RARRBOX,ud,\
 (add1)?&qsfmt_shortp1:&qsfmt_short,NULL}

#define QTSL_INIT1(mi,ma,v,add1,ud,aro,wrap) \
 {mi,ma,(v),aro,12,wrap?QSL_MINWRAP|QSL_MAXWRAP:0,ud,\
 (add1)?&qsfmt_shortp1:&qsfmt_short}

/* qslider functions */

void see_qslider(Button *b);
void feel_qslider(Button *b);
bool in_left_arrow(Button *slb);
bool in_right_arrow(Button *slb);


/* struct for string request field button data This is put in the "data" field
 * for the button.  The next field to "tab" to when the tab key is hit is 
 * placed in the "group" pointer for the button NULL if no next button to tab
 * to */

struct stringq
	{
	SHORT pxoff, pyoff;	/* pixel starting position */
	SHORT dcount;	/* # of characters displayed */
	SHORT bcount;	/* # of characters that can fit in string not including
					 * terminating NULL */
	SHORT ccount;	/* # of characters currently in string */
	char *string;
	char *undo;
	SHORT cpos;		/* pointer position on screen */
	SHORT dpos;		/* pol of leftmost visible character */
	};
typedef struct stringq Stringq;
#define STRINGQSZ 128

#define STQ_LJUST	-1
#define STQ_CENTER  -2

/* some stuff for number string fields */

typedef struct numq {
	void *val;
} Numq;

#define NUMQ_INIT(val) { val }

void see_numq(Button *b);
bool feel_numq(Button *b);

/* routines to wait on input while re-loading cursor and iostate info as cursor
 * passes over windows or menus work same way as routines in input.c */

Wndo *wait_wndo_input(ULONG ioflags);
int anim_wndo_input(ULONG waitflags, ULONG forceflags, 
				    int maxfields, FUNC func, void *funcdata);

Errcode init_muscreen(Wscreen *w); /* prepare a screen for menus */
void cleanup_muscreen(Wscreen *w); /* cleans up menu portion of screen */
void close_all_menus(Wscreen *w,LONG code); /* will close all menus */

/* basic open and close functions to open a menuhdr's menuwndo 
 * add menu to a group and draw it */

Errcode open_menu(Wscreen *screen, Menuhdr *mh,Mugroup *group, Wndo *over);
void close_menu_code(Menuhdr *md, LONG code);
void close_menu(Menuhdr *md);

extern void hide_menu(Menuhdr *m);
extern Errcode show_menu(Menuhdr *m);
extern void toggle_menu(void);
extern void draw_menu(Menuhdr *mh);
extern void draw_menupull(Menuhdr *mh);

extern void scale_button(Button *b, Rscale *scale);
extern void scale_button_list(Button *b, Rscale *scale);

/* set explicit button to hit on next tab key */

extern void mb_set_tabnext(Button *b, Button *next);

/* positioner functions for closed menus */

void menu_to_point(Wscreen *s,Menuhdr *mh,SHORT centx,SHORT centy);
void menu_to_cursor(Wscreen *s,Menuhdr *mh);
void menu_to_reqpos(Wscreen *s,Menuhdr *mh);
void menu_to_quickcent(Menuhdr *mh);

/* simply a test to see if it is open or closed */
#define MENU_ISOPEN(mh) ((mh)->node.next != NULL)

/* test for whether menu window is up on screen */
#define MENU_ISUP(mh) ((mh)->mw != NULL)

/** menugroup calls ********/

void push_group(Wscreen *ws,Mugroup *mg);
void pop_group(Mugroup *mg);
void close_group_code(Mugroup *mg,LONG code);
void close_group(Mugroup *mg);
void mh_gclose_code(Menuhdr *mh, LONG code);

void hide_group(Mugroup *mg);
Errcode show_group(Mugroup *mg);
bool cgroup_hidden(Wscreen *ws);
void stack_hide_cgroup(Wscreen *ws);
bool stack_show_cgroup(Wscreen *ws);

/* do menu io loops with initial menu and initial button and pull */
LONG do_menuloop(Wscreen *screen,Menuhdr *menu,Button *initb,
				 Menuhdr *pull, FUNC default_doinput );
LONG do_reqloop(Wscreen *screen,Menuhdr *menu,Button *initb,
				Menuhdr *pull, FUNC default_doinput );

/* these are for determining hits in buttons for menu relative x and y **/
#define ptin_button(b,x,y) ptin_rect((Rectangle*)&((b)->RECTSTART),(x),(y))

bool cursin_menu(Menuhdr *m); /* is screen cursor in visible menu? */

/* functions for seebg in menus */
void seebg_none(Menuwndo *m);
void seebg_white(Menuwndo *m);
void seebg_bblack(Menuwndo *m);
void seebg_ulwhite(Menuwndo *m);

extern void mb_make_clip(Button *b, Clipbox *cb);
extern void mb_make_iclip(Button *b, Clipbox *cb);

/* some domenu functions return TRUE if ate a key 0 if not */

#define DOBUTTON_INPUT  (MBPEN|MBRIGHT|KEYHIT)
int do_menubuttons(Menuhdr *mh); /* this is default if domenu is NULL 
								  * if default set all input flaged to menu
								  * is "eaten" if this function is set ex
								  * plicitly only input used by buttons is
								  * "eaten" */
int menu_dopull(Menuhdr *mh); /* this is default if domenu is NULL */

/* flags manipulators */

bool set_button_disable(Button *b, bool disable);
void set_mbtab_disables(Button **bt, bool disable);
void draw_button_disable(Button *b, bool disable);
void enable_button(Button *b);
void disable_button(Button *b);
#ifdef SLUFFED
void enable_mbtab(Button **bt);
void disable_mbtab(Button **bt);
#endif /* SLUFFED */

#define DHIT_MICROS 400

Button *find_button(Button *m, SHORT id);

/* button allocators */

void free_buttonlist(Button **pb);
Errcode clone_buttonlist(Button *toclone,Button **pb);

/* functions to get gui colours */

extern int wbg_textcolor(Button *b);
extern int mc_bright(Button *b);
extern int mc_white(Button *b);
extern int mc_red(Button *b);
extern int mc_grey(Button *b);
extern int mc_black(Button *b);

/* functions to call the seemes */

void draw_buttonlist(Button *first);
void draw_button(Button *b);
void draw_buttontop(Button *b);

/**** some seeme drawing functions and sub functions ****/

extern void color_block1(Button *m, SHORT color);
extern void m2color_block(Button *m, SHORT fcolor, SHORT bcolor);
extern void a_block(Button *b, Pixel color);
extern void mc_block(Button *b, int mc_color);
extern void a_frame(Button *b, Pixel color);
extern void mc_frame(Button *b, int mc_color);
extern void safe_mc_frame(Button *b, int mc_color);
extern void white_block(Button *b);
extern void black_block(Button *b);
extern void grey_block(Button *b);
extern void mb_isquare(Button *b, int color);
extern void mb_dcorner(Button *b, int color);
extern void mb_dinside(Button *b, int color);
extern void wbg_ncorner_back(Button *b);

int mb_centext(Button *b, Pixel color, char *string);
void black_ltext(Button *b);
void grey_ctext(Button *b);
void black_ctext(Button *b);
void ncorner_text(Button *b);
void wbg_ncorner_text(Button *b);
void wbg_ncorner_ltext(Button *b);
void ccorner_text(Button *b);
void dcorner_text(Button *b);
void ncorner_short(Button *b);
void ncorner_short_plus1(Button *b);
void ccorner_short(Button *b);
void gbshortint(Button *b);
void black_label(Button *b);
void black_leftlabel(Button *b);
void black_pathlabel(Button *b);

void mb_centimage(Button *b, int color, Image *image);
void see_centimage(Button *b);
void wbg_ncorner_image(Button *b);
void ncorner_image(Button *b);
void ccorner_image(Button *b);

extern void change_mode(Button *b);
extern void hilight(Button *m);
extern void mb_set_hilite(Button *b, bool hilite);
extern void mb_hang_chiles_oset(Button *m,SHORT xoset, SHORT yoset);
extern void hang_children(Button *b);
extern void bg_hang_children(Button *b);

/**** some feelme optme functions and sub functions ****/

Menuhdr *get_button_hdr(Button *b);   /* gets header mb attached to if open */
Menuwndo *get_button_wndo(Button *b); /* gets menuwndo mb drawn to if open */

/* close menus from buttons */
void mb_close_code(Button *b,LONG code);
void mb_close_ok(Button *b);
void mb_close_cancel(Button *b);

/* close groups from menus */
void mb_gclose_code(Button *b,LONG code);
void mb_gclose_ok(Button *b);
void mb_gclose_cancel(Button *b, void *dat);
void mb_gclose_identity(Button *b);

void mb_draw_menu(Button *b);
void mb_hide_menu(Button *b);
void mb_hide_group(Button *b);
void mb_show_group(Button *b);
void mb_move_menu(Button *b); /* marqui move menu will allow move off screen */

/* same but clips to screen bounds */
extern void mb_clipmove_menu(Button *b, void *dat);

void mb_menu_to_bottom(Button *b); /* move menu to bottom of screen */

/* special titlebar button stuff */

typedef struct titbar_group {
	void (*moveit)(Button *b,void *data);
	void (*closeit)(Button *b,void *data);
	void *data;
} Titbar_group;

extern void see_titlebar(Button *b);
extern void hang_see_title(Button *b);
extern void feel_titlebar(Button *b);
extern void feel_req_titlebar(Button *b);

extern Titbar_group tbg_moveclose;

/* gets stack head */
#define curr_group(ws) ((Mugroup *)see_head(&((ws)->gstack))) 

/* button group highlighting functions */

extern void toggle_bgroup(Button *b);
extern void mb_unhi_group(Button *b);
extern void mb_hi_group(Button *b);
extern void mb_draw_ghi_group(Button *b);
extern void mh_draw_group(Menuhdr *mh, void *group);

/*** stock menu builders ***/

Errcode build_qchoice(Wscreen *s, Menuhdr **pmh, char *header, char **choices, 
				  	 int ccount, VFUNC *feelers,
					  bool hide_on_hit ,
					 USHORT *flags);
/* bits for flags parameter to build_qchoice */
#define QCF_DISABLED (1<<0)
#define QCF_ASTERISK (1<<1)

void cleanup_qchoice(Menuhdr *qc);

/* qnumber functions */

Errcode build_qnumreq(Wscreen *s, Menuhdr **pmh, char *hailing,
					  char **ok_cancel, Image **arrows,
					  SHORT initial, SHORT min, SHORT max,
					  Errcode (*update)(void *uddat, SHORT val), void *uddat);

SHORT get_qnumval(Menuhdr *mh);
void cleanup_qnumreq(Menuhdr *mh);

/* stringq functions */

extern Errcode
build_qstrreq(Wscreen *s, Menuhdr **pmh,
		char *hailing, char **ok_cancel, char *strbuf, int strlength);

void cleanup_qstrreq(Menuhdr *mh);
int feel_string_req(Button *sqb);
void stringq_revert_to_undo(Stringq *stq);
void init_stq_string(Stringq *stq);
void undo_stringq(Button *m,Button *stq_item);
void set_stq_string(Stringq *stq, char *buf);
void setf_stringq(Button *sqb,int drawit,char *fmt,...);

/* return value flags for feel_string_req() */
#define STQ_ENTER 	0x0001
#define STQ_ALTERED	0x0002
#define STQ_TAB		0x0004
#define STQ_ESCAPE	0x0008

void see_string_req(Button *sqb);
void see_hailing(Button *b);

void offset_button_list(Button *b, SHORT x, SHORT y);

/********************* pull down menu structures ******************/


/* This is the tree structure that is the core of the pull-down system */

typedef struct pull {
	struct pull *next;
	struct pull *children;
	void *data;  /* actually just some old pointer */
	VFUNC see;
	SHORT key_equiv;
	UBYTE flags;
	RECT_FIELDS;
	SHORT key2;
	SHORT id;
} Pull;
#define PULL_DISABLED  0x01
#define PULL_DOESKEYS  0x02
#define PULL_HILIT	0x04

Errcode new_pull(Pull **ppull, char *inits);
/* allocate and initialize a Pull. 
 * If the string inits is non-NULL, then
 * allocate extra space at end of pull for 
 * string, and set the Pull data pointer. */

extern void
pullfmt(Menuhdr *mp, int subspace, int charw, int charh,
		Rectangle *screen_rect);

/* flags manipulators */
Pull *id_to_pull(Menuhdr *mh, SHORT id);
void set_pul_disable(Menuhdr *mh, SHORT id, bool disable);
void set_pultab_disable(Menuhdr *mh, 
	SHORT *ids, int id_count, bool disable);
void set_leaf_disable(Menuhdr *mh, SHORT leafid, bool disable);
void pul_xflag(Menuhdr *mh, SHORT id, bool xflag);
void pultab_xoff(Menuhdr *mh, SHORT *ids, int id_count);

void scale_pull(Menuhdr *mh, int subspace);
/* scale pulldown to vb.screen... */


/* local data structure passed into all pull->sees when called */

#define PULL_MAXLEVELS	4

typedef struct pullwork {
	Menuhdr *root;	 	/* the root menuheader */	
	Rcel *port;			/* where to draw it */
	Wscreen *screen;	/* the screen */
	struct vfont *font;     /* the font to use */
	SHORT spwidth;		/* width of a ' ' character in font */
	SHORT cheight;		/* character height in font */
	SHORT level;		/* current recursion level 0 = bottom */
	SHORT *ix;  		/* the pull index of hit for each level */
	Raster     *behind[PULL_MAXLEVELS];	/* rasters for save areas */
	Pull *leaf_parent;
	SHORT lpx,lpy;
} Pullwork;

/* functions for (*pull->see)(inx x,int y,Pull *p,Pullwork *pw);  */

extern void init_pullwork(Pullwork *pw, Menuhdr *mh);
extern void pull_leftext(int x, int y, Pull *p, Pullwork *pw);
extern void pull_toptext(int x, int y, Pull *p, Pullwork *pw);

/*some functions to put into pull->see */
extern void pull_oblock(int x, int y, Pull *p, Pullwork *pw);
extern void pull_midline(int x, int y, Pull *p, Pullwork *pw);
extern void see_pull(int x, int y, Pull *p, Pullwork *pw);

	/* border width around string buttons */
#define MB_IBORDER 1

#endif
