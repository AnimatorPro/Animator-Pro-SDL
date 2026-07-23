/*****************************************************************************
 * badver.c - Test POE module with wrong version number
 *            This should trigger Err_poco_lib_version
 ****************************************************************************/

#include "errcodes.h"
#include "pocorex.h"

static void dummy_func(void)
{
	/* Empty function */
}

static Lib_proto poe_calls[] = {
	{dummy_func, "void DummyFunc(void);"},
};

/* Manually create structure with wrong version */
static Pocorex badver_pocorex = {{
									 100,    /* WRONG VERSION - should be POCOREX_VERSION (200) */
									 NOFUNC, /* init */
									 NOFUNC, /* cleanup */
									 "BadVer POE" /* id_string */
								 },
								 {
									 NULL,                                  /* next */
									 NULL,                                  /* name */
									 poe_calls,                             /* lib */
									 sizeof(poe_calls) / sizeof(Lib_proto), /* count */
									 NULL,                                  /* init */
									 NULL,                                  /* cleanup */
									 NULL,                                  /* local_data */
									 {NULL, NULL},                          /* resources */
									 NULL                                   /* rexhead */
								 }};

// deliberately not using Setup_Pocorex to trigger the error

#ifdef _WIN32
__declspec(dllexport)
#endif
Pocorex* poco_rexlib_get(void)
{
	return &badver_pocorex;
}
