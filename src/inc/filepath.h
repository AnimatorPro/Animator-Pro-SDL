#ifndef FILEPATH_H
#define FILEPATH_H

#ifndef STDTYPES_H
	#include "stdtypes.h"
#endif

/* Maximum directory name size ms-dos will accept */
#define DEV_NAME_LEN 2+1 	/* for ms dos includes null terminator */

#define MAX_DIR_LEN 66
#define MAX_FILE_NAME_LEN 8
#define MAX_SUFFIX_NAME_LEN 3
#define FILE_NAME_SIZE (8+1+3+1)

/* size of a string big enough to hold any path/file name */
#if defined(__WATCOMC__)
#define PATH_SIZE 80
#else /* __WATCOMC__ */
#define PATH_SIZE PATH_MAX
#endif /* __WATCOMC__ */

#ifdef _MSC_VER
#define PATH_MAX 1024
#define PATH_SIZE PATH_MAX
#endif

/* string big enough for a wildcard */
#define WILD_SIZE 16

#if defined(__WATCOMC__)
#define DIR_DELIM       '\\'
#define DIR_DELIM2      '/'
#define DIR_DELIM_STR   "\\"
#else /* __WATCOMC__ */
#define DIR_DELIM       '/'
#define DIR_DELIM2      '\\'
#define DIR_DELIM_STR   "/"
#endif /* __WATCOMC__ */

#define DEV_DELIM	':'
#define DEV_DELIM_STR	":"
#define SUFF_DELIM  '.'
#define SUFF_DELIM_STR  "."

enum FilePathType {
	/* No path prefix.  Current working directory. */
	FILEPATH_NO_PREFIX,

	/* User's config directory.
	 * e.g. ~/.config/pjpaint/
	 */
	FILEPATH_CONFIG_DIR,

	/* Shared resource directory.  Read only.
	 * e.g. /usr/local/share/pjpaint/
	 */
	FILEPATH_RESOURCE_DIR,

	/* Temporary directory.
	 * e.g. /dev/shm/pjpaint/
	 */
	FILEPATH_TEMP_DIR,

	/* Absolute path without a drive name.
	 * e.g. / or \
	 */
	FILEPATH_ROOT,

	/* Absolute path with a drive name. (DOS)
	 * e.g. C:\ or C:/
	 */
	FILEPATH_DOS,

	FILEPATH_NUM_TYPES
};

struct filepath;
typedef struct filepath FilePath;

extern FilePath *filepath_create_from_string(const char *str);
extern Errcode filepath_destroy(FilePath *filepath);
extern Errcode filepath_append(FilePath *filepath, const char *str);
extern Errcode filepath_drop_tail(FilePath *filepath);

extern Errcode
filepath_to_cstr(const FilePath *filepath, char delim, char *str, size_t n);

#ifdef REXLIB_CODE 

/* Items in _a_a_loadpath: get_load_path() is the only call 
 * now in _a_a_loadpath **/

/* get_load_path() returns a poiner to a static buffer containing the path
 * the rexlib code was loaded from ie "d:\dir1\dir2\yourcode.drv" 
 * it may not be a full path but contains the path exactly as supplied by
 * the loading program to load_rexlib() Do not declare this library if
 * you do not need it */

char *pj_get_load_path(void);

/***** items in _a_a_syslib ******/

#endif  /* REXLIB_CODE */


/* returns pointer to "dot" of suffix or terminating '\0' if no suffix */
char *pj_get_path_suffix(char *path); 

/* gets pointer to last name in a path. */
char *pj_get_path_name(char *path);
void remove_path_name(char *path);


#ifdef BIG_COMMENT /*************************************/

sample usage: 
{
char path[PATH_SIZE];  /* contains file path including file name */

 /* get path into a local buffer */

 	strcpy(path, pj_get_load_path());

 /* replace suffix on path */

	strcpy(pj_get_path_suffix(path),".NEW"); 

 /* replace last name on path */

	strcpy(pj_get_path_name(path), "NEWNAME.XXX");

 /* truncate file name from path */

 	*pj_get_path_name(path) = 0;

 /* truncate suffix from from path */

 	*pj_get_path_suffix(path) = 0;

#endif /* BIG_COMMENT ************************************/


#ifdef PRIVATE_CODE 

/* gets length of all but the last name in a path */
int get_drawer_len(char *path);

/* gets length of all but the suffix in the path */
int path_prefix_len(char *path);

void remove_suffix(char *path);
bool suffix_in(char *path, char *suff);
int parse_to_semi(char **in, char *out,int maxlen);

bool pj_valid_suffix(char *suff);
bool has_a_suffix(char *path);
char *fix_suffix(char *path);

void pj_set_path_name(char *path, char *name); /* changes, adds name to path */
bool pj_name_in_path(char *path, char *name); /* true if path has name */

Errcode get_path_device(char *path,char *device);
Errcode get_full_path(const char *path, char *fullpath);
Errcode get_dir(char *path);
Errcode add_subpath(char *drawer, char *subpath, char *outpath);
Errcode full_path_name(char *drawer,char *subpath,char *fullpath);
Errcode make_file_path(char *drawer, char *name, char *outpath);
Errcode full_file_path(char *drawer, char *name, char *fullpath);
Errcode make_good_dir(char *path);

Errcode fnsplit(char *path, char *device, char *dir, char *file, char *suffix);
Errcode split_path(char *p1, char *device, char *dir);
Errcode dice_file_name(char *path, char *device, char *dir, char *file,
	char *suffix);
Errcode split_copy_path(char *path,char *drawer,char *name);
int pj_inc_filename(char *path);

extern int _fp_get_path_devlen(const char *path);
extern int _fp_parse_device(char **pfn, char *device);
extern Errcode _fp_parse_dir(char **pfn, char *dir);

/* PRIVATE_CODE */ #endif

#endif /* FILEPATH_H */
