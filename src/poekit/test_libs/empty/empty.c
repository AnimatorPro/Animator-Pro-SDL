/*****************************************************************************
 * empty.c - Test POE module with no functions
 *           This should trigger Err_poco_lib_empty
 ****************************************************************************/

#include "errcodes.h"
#include "pocorex.h"

/* Manually set up the structure with count=0 and lib=NULL */
static Pocorex empty_pocorex = {{
									POCOREX_VERSION, /* correct version */
									NOFUNC,          /* init */
									NOFUNC,          /* cleanup */
									"Empty POE"      /* id_string */
								},
								{
									NULL,         /* next */
									NULL,         /* name */
									NULL,         /* lib = NULL (EMPTY) */
									0,            /* count = 0 (EMPTY) */
									NULL,         /* init */
									NULL,         /* cleanup */
									NULL,         /* local_data */
									{NULL, NULL}, /* resources */
									NULL          /* rexhead */
								}};

#ifdef _WIN32
__declspec(dllexport)
#endif
Pocorex* poco_rexlib_get(void)
{
	return &empty_pocorex;
}
