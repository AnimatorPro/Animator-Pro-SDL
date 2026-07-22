/*****************************************************************************
 * pocopicdrive.c - Host-side Poco functions for picture file I/O.
 *
 * These functions replace the old pdracces.poe module by providing PicLoad,
 * PicSave, PicGetSize, etc. as built-in Poco host functions.  They delegate
 * to the Local_pdr drivers already linked into the ani executable (GIF, BMP,
 * JPEG, PCX, PNG, FLC, PIC) via the picdrive host functions.
 ****************************************************************************/

#include "errcodes.h"
#include "jimk.h"
#include "animinfo.h"
#include "cmap.h"
#include "fli.h"
#include "filepath.h"
#include "picdrive.h"
#include "pocolib.h"
#include "pocoface.h"

#include <string.h>

extern Errcode builtin_err;

/*----------------------------------------------------------------------------
 * module-level state (mirrors the old pdracces.c static data)
 *--------------------------------------------------------------------------*/

static Pdr* cur_pdr = NULL;
static char cur_pdr_name[64];

/*----------------------------------------------------------------------------
 * helpers
 *--------------------------------------------------------------------------*/

static const char* get_suffix(const char* path)
{
	const char* dot = NULL;
	while (*path) {
		if (*path == '.') {
			dot = path;
		} else if (*path == '/' || *path == '\\') {
			dot = NULL;
		}
		++path;
	}
	return dot;
}

/*----------------------------------------------------------------------------
 * po_pic_driver_clear — reset the cached PDR pointer.  Local PDRs are
 * statically linked, so there is nothing to free.
 *--------------------------------------------------------------------------*/
static void po_pic_driver_clear(void)
{
	cur_pdr = NULL;
	cur_pdr_name[0] = '\0';
}

/*----------------------------------------------------------------------------
 * PicDriverSet — select a specific PDR by name (e.g. "GIF.PDR")
 *--------------------------------------------------------------------------*/
static Errcode po_pic_driver_set(char* pdrname)
{
	Errcode err;

	if (pdrname == NULL) {
		return builtin_err = Err_null_ref;
	}

	/* already loaded? */
	if (cur_pdr != NULL) {
		if (0 == strcmp(pdrname, cur_pdr_name)) {
			return Success;
		}
		cur_pdr = NULL;
	}

	err = load_pdr(pdrname, &cur_pdr);
	if (err < Success) {
		return err;
	}

	strncpy(cur_pdr_name, pdrname, sizeof(cur_pdr_name) - 1);
	cur_pdr_name[sizeof(cur_pdr_name) - 1] = '\0';
	return Success;
}

/*----------------------------------------------------------------------------
 * suffix-to-PDR-name mapping table (matches the SDL picdrivers we register
 * in main.c via add_local_pdr)
 *--------------------------------------------------------------------------*/

typedef struct {
	const char* suffix;
	const char* pdr_name;
} SuffixToPdr;

static const SuffixToPdr suffix_table[] = {
	{".gif", "GIF.PDR"},  {".bmp", "BMP.PDR"},   {".rle", "BMP.PDR"},
	{".pcx", "PCX.PDR"},  {".jpg", "JPEG.PDR"},  {".jpeg", "JPEG.PDR"},
	{".png", "PNG.PDR"},  {".tga", "TARGA.PDR"}, /* if you add targa later */
	{".tif", "TIFF.PDR"},                        /* if you add tiff later */
	{".flc", "=FLC.PDR"}, {".fli", "=FLC.PDR"},  {".pic", "=PIC.PDR"},
	{NULL, NULL},
};

/*----------------------------------------------------------------------------
 * PicDriverDetect — find the right PDR for a picture path (by suffix)
 *--------------------------------------------------------------------------*/
static Errcode po_pic_driver_detect(char* picpath)
{
	const char* suf;
	const SuffixToPdr* p;

	if (picpath == NULL) {
		return builtin_err = Err_null_ref;
	}

	suf = get_suffix(picpath);
	if (suf == NULL || *suf == '\0') {
		return Err_pic_unknown;
	}

	for (p = suffix_table; p->suffix != NULL; ++p) {
		if (strcasecmp(suf, p->suffix) == 0) {
			return po_pic_driver_set((char*)p->pdr_name);
		}
	}

	return Err_pic_unknown;
}

/*----------------------------------------------------------------------------
 * PicGetSize — get width, height, and depth of a picture file without
 *              loading it onto the screen.
 *--------------------------------------------------------------------------*/
static Errcode po_pic_get_size(char* path, int* width, int* height, int* depth)
{
	Errcode err;
	Image_file* ifile = NULL;
	Anim_info ainfo;

	if (path == NULL || width == NULL || height == NULL || depth == NULL) {
		return builtin_err = Err_null_ref;
	}

	err = po_pic_driver_detect(path);
	if (err < Success) {
		return err;
	}

	get_screen_ainfo(vb.pencel, &ainfo);
	err = pdr_open_ifile(cur_pdr, path, &ifile, &ainfo);
	if (err < Success) {
		return err;
	}

	*width = ainfo.width;
	*height = ainfo.height;
	*depth = ainfo.depth;

	pdr_close_ifile(&ifile);
	return Success;
}

/*----------------------------------------------------------------------------
 * PicLoad — load a picture into the screen (or a specified screen).
 *--------------------------------------------------------------------------*/
static Errcode po_pic_load(char* path, void* screen)
{
	Errcode err;
	Image_file* ifile = NULL;
	Anim_info ainfo;
	bool allow_retry = true;

RETRY:
	if (cur_pdr == NULL) {
		err = po_pic_driver_detect(path);
		if (err < Success) {
			return err;
		}
		allow_retry = false;
	}

	if (path == NULL) {
		return builtin_err = Err_null_ref;
	}

	if (screen == NULL) {
		screen = vb.pencel;
	}

	get_screen_ainfo((Rcel*)screen, &ainfo);
	err = pdr_open_ifile(cur_pdr, path, &ifile, &ainfo);
	if (err < Success) {
		if (allow_retry) {
			cur_pdr = NULL;
			goto RETRY;
		}
		return err;
	}

	if (ainfo.width == 0 || ainfo.height == 0) {
		pdr_close_ifile(&ifile);
		return Err_format;
	}

	pj_set_rast((Rcel*)screen, 0);
	err = pdr_read_first(ifile, (Rcel*)screen);

	dirties();
	pdr_close_ifile(&ifile);
	return err;
}

/*----------------------------------------------------------------------------
 * PicSave — save the screen (or a specified screen) to a picture file.
 *--------------------------------------------------------------------------*/
static Errcode po_pic_save(char* path, void* screen)
{
	Errcode err;
	Image_file* ifile = NULL;
	Anim_info ainfo;

	if (cur_pdr == NULL) {
		err = po_pic_driver_detect(path);
		if (err < Success) {
			return err;
		}
	}

	if (path == NULL) {
		return builtin_err = Err_null_ref;
	}

	if (screen == NULL) {
		screen = vb.pencel;
	}

	get_screen_ainfo((Rcel*)screen, &ainfo);

	if (cur_pdr->spec_best_fit != NULL) {
		cur_pdr->spec_best_fit(&ainfo);
	}

	err = pdr_create_ifile(cur_pdr, path, &ifile, &ainfo);
	if (err < Success) {
		return err;
	}

	err = pdr_save_frames(ifile, (Rcel*)screen, 1, NULL, NULL, NULL);
	pdr_close_ifile(&ifile);
	return err;
}

/*----------------------------------------------------------------------------
 * from packcmap.c
 *--------------------------------------------------------------------------*/
extern Errcode po_pack_colortable(int* source, int source_count, int* dest, int dest_count);

/*----------------------------------------------------------------------------
 * library protos — these become built-in Poco functions
 *--------------------------------------------------------------------------*/

static Lib_proto po_picdrive_protos[] = {
	{po_pic_driver_clear, "void    PicDriverUnload(void);"},
	{po_pic_driver_set, "Errcode PicDriverSet(char *pdrname);"},
	{po_pic_driver_detect, "Errcode PicDriverDetect(char *picpath);"},
	{po_pic_load, "Errcode PicLoad(char *path, Screen *screen);"},
	{po_pic_save, "Errcode PicSave(char *path, Screen *screen);"},
	{po_pic_get_size,
	 "Errcode PicGetSize(char *path,"
	 " int *width, int *height, int *depth);"},
	{po_pack_colortable,
	 "Errcode PackColorTable(int *source, int source_count,"
	 " int *dest, int dest_count);"},
};

Poco_lib po_picdrive_lib = {
	NULL,
	"Picture Driver",
	po_picdrive_protos,
	Array_els(po_picdrive_protos),
	NULL,                /* init */
	po_pic_driver_clear, /* cleanup — reset cached PDR on script exit */
};
