/*****************************************************************************
 * nullret.c - Test POE module that returns NULL from entry point
 *             This should trigger Err_poco_lib_invalid
 ****************************************************************************/

#include "pocorex.h"

/* Entry point that returns NULL to simulate invalid library */
#ifdef _WIN32
__declspec(dllexport)
#endif
Pocorex* poco_rexlib_get(void)
{
	return NULL; /* Intentionally return NULL */
}
