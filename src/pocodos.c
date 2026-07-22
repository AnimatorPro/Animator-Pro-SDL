
#include <string.h>

#include "errcodes.h"
#include "ptrmacro.h"
#include "memory.h"
#include "jfile.h"
#include "pocoface.h"
#include "pocolib.h"
#include "menus.h"
#include "inkaid.h"
#include "resource.h"
#include "wildlist.h"

extern Poco_lib po_dos_lib;
extern char po_current_program_path[]; /* defined in qpoco.c */

/*****************************************************************************
 * int DirList(char ***list, char *wild, Boolean get_dirs);
 *
 * Note: This function uses Popot* for the output array parameter because
 * the Poco runtime needs to track array bounds for the returned string list.
 ****************************************************************************/
static int po_dir_list(Popot* ppdest, char* pat, int get_dirs)
{
	//! TODO: Implement directory listing
	(void)ppdest;
	(void)pat;
	(void)get_dirs;
	return 0;
}

/*****************************************************************************
 * void FreeDirList(char ***list);
 *
 * Note: Uses Popot* for the array parameter to match DirList's output type.
 ****************************************************************************/
static void po_free_dir_list(Popot* list)
{
	if (list == NULL) {
		builtin_err = Err_null_ref;
		return;
	}
	if (list->pt != NULL) {
		pj_free(list->pt);
		list->pt = list->min = list->max = NULL;
	}
}

/*****************************************************************************
 * ErrCode GetDir(char *dir);
 ****************************************************************************/
static Errcode po_get_dir(char* dir)
{
	if (dir == NULL) {
		return builtin_err = Err_null_ref;
	}
	return get_dir(dir);
}

/*****************************************************************************
 * void GetResourceDir(char *dir);
 ****************************************************************************/
static void po_get_resource_dir(char* dir)
{
	if (dir == NULL) {
		builtin_err = Err_null_ref;
		return;
	}
	strcpy(dir, resource_dir);
}

/*****************************************************************************
 * ErrCode SetDir(char *dir);
 ****************************************************************************/
static Errcode po_set_dir(char* dir)
{
	//! TODO: Implement SetDir
	if (dir == NULL) {
		return builtin_err = Err_null_ref;
	}
	// return change_dir(dir);
	return builtin_err = Err_null_ref;
}

/*****************************************************************************
 * ErrCode DosDelete(char *filename);
 ****************************************************************************/
static Errcode po_delete(char* name)
{
	if (name == NULL) {
		return builtin_err = Err_null_ref;
	}
	return pj_delete(name);
}

/*****************************************************************************
 * ErrCode DosRename(char *old, char *new);
 ****************************************************************************/
static Errcode po_rename(char* oldname, char* newname)
{
	if (oldname == NULL || newname == NULL) {
		return builtin_err = Err_null_ref;
	}
	return pj_rename(oldname, newname);
}

/*****************************************************************************
 * ErrCode DosCopy(char *source, char *dest);
 ****************************************************************************/
static Errcode po_dos_copy(char* source, char* dest)
{
	char* errfile;

	if (source == NULL || dest == NULL) {
		return builtin_err = Err_null_ref;
	}
	return pj_cpfile(source, dest, &errfile);
}

/*****************************************************************************
 * Boolean DosExists(char *filename);
 ****************************************************************************/
static bool po_exists(char* name)
{
	if (name == NULL) {
		builtin_err = Err_null_ref;
		return false;
	}
	return pj_exists(name);
}

/*****************************************************************************
 * ErrCode fnsplit(char *path, char *device, char *dir, char *file, char *suf);
 ****************************************************************************/
static Errcode po_fnsplit(char* path, char* device, char* dir, char* file, char* suffix)
{
	if (path == NULL || device == NULL || dir == NULL || file == NULL || suffix == NULL) {
		return builtin_err = Err_null_ref;
	}
	return fnsplit(path, device, dir, file, suffix);
}

/*****************************************************************************
 * ErrCode fnmerge(char *path, char *device, char *dir, char *file, char *suf);
 ****************************************************************************/
static Errcode po_fnmerge(char* path, char* device, char* dir, char* file, char* suffix)
{
	size_t total_len;
	char* dev_str = device ? device : "";
	char* dir_str = dir ? dir : "";
	char* file_str = file ? file : "";
	char* suf_str = suffix ? suffix : "";

	if (path == NULL) {
		return builtin_err = Err_null_ref;
	}

	total_len = strlen(dev_str) + strlen(dir_str) + strlen(file_str) + strlen(suf_str);
	if (total_len >= PATH_SIZE) {
		return Err_dir_too_long;
	}

	strcpy(path, dev_str);
	strcat(path, dir_str);
	strcat(path, file_str);
	strcat(path, suf_str);
	return Success;
}

/*****************************************************************************
 * void GetProgramDir(char *dir);
 ****************************************************************************/
static void po_get_program_dir(char* dir)
{
	if (dir == NULL) {
		builtin_err = Err_null_ref;
		return;
	}
	strcpy(dir, po_current_program_path);
}

/*----------------------------------------------------------------------------
 * library protos...
 *
 * Maintenance notes:
 *	To add a function to this library, add the pointer and prototype string
 *	TO THE END of the following list. Then go to POCOLIB.H and add a
 *	corresponding prototype to the library structure typedef therein which
 *	matches the name of the structure below.
 *
 *	The C function signature must exactly match the Poco prototype string,
 *	using direct C types (char*, int*, void*, etc.) instead of the legacy
 *	Popot type. The libffi runtime calls these functions directly.
 *	See docs/coding-style-c.md "Poco Library Functions" for details.
 *
 *	DO NOT ADD NEW PROTOTYPES OTHER THAN AT THE END OF AN EXISTING STRUCTURE!
 *	DO NOT DELETE A PROTOTYPE EVER! (The best you could do would be to point
 *	the function to a no-op service routine). Breaking these rules will
 *	require the recompilation of every POE module in the world.
 *--------------------------------------------------------------------------*/

PolibDos po_libdos = {
	po_fnsplit,
	"ErrCode fnsplit(char *path, char *device, char *dir, char *file, char *suf);",
	po_fnmerge,
	"ErrCode fnmerge(char *path, char *device, char *dir, char *file, char *suf);",
	po_exists,
	"Boolean DosExists(char *filename);",
	po_dos_copy,
	"ErrCode DosCopy(char *source, char *dest);",
	po_delete,
	"ErrCode DosDelete(char *filename);",
	po_rename,
	"ErrCode DosRename(char *old, char *new);",
	po_set_dir,
	"ErrCode SetDir(char *dir);",
	po_get_dir,
	"ErrCode GetDir(char *dir);",
	po_dir_list,
	"int     DirList(char ***list, char *wild, Boolean get_dirs);",
	po_free_dir_list,
	"void    FreeDirList(char ***list);",
	po_get_resource_dir,
	"void    GetResourceDir(char *dir);",
	po_get_program_dir,
	"void    GetProgramDir(char *dir);",
};


Poco_lib po_dos_lib = {
	NULL,
	"DOS",
	(Lib_proto*)&po_libdos,
	POLIB_DOS_SIZE,
};
