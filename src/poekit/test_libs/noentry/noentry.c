/*****************************************************************************
 * noentry.c - Test POE module without an entry point (missing poco_rexlib_get)
 *             This should trigger Err_poco_lib_no_entry
 ****************************************************************************/

#include <stdio.h>

/* Deliberately NOT including pocorex.h and NOT defining poco_rexlib_get */

void some_random_function(void)
{
	printf("This function exists but no entry point!\n");
}
