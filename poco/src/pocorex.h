/*****************************************************************************
 * POCOREX.H - Compatibility-only legacy POE module ABI.
 *
 * Deprecated for new modules: export poco_module_get from <poco/poco.h> and
 * return a PocoModuleDescriptor.  Pocorex/poco_rexlib_get remain available
 * only while existing Animator POE modules migrate.
 ****************************************************************************/

#ifndef POCOREX_H
#define POCOREX_H

#ifndef STDTYPES_H
#include "stdtypes.h"
#endif

#ifndef POCOLIB_H
#include "pocolib.h"
#endif

#if defined(_WIN32)
#define POCO_EXPORT __declspec(dllexport)
#else
#define POCO_EXPORT __attribute__((visibility("default")))
#endif

typedef struct pocorex_hdr {
	USHORT version;
	/* Equivalent of Animator's EFUNC and VFUNC */
	Errcode (*init)();
	void (*cleanup)();
	char* id_string;
} Pocorex_hdr;

#define POCOREX_VERSION 200

typedef struct pocorex {
	Pocorex_hdr hdr;
	Poco_lib lib;
} Pocorex;

typedef Pocorex* (*Poco_rexlib_get_func)(void);

#define Setup_Pocorex(init, cleanup, libname, libprotos)                         \
	static char _l_name[] = libname;                                             \
	Pocorex rexlib_header = {                                                    \
		{POCOREX_VERSION, init, cleanup, _l_name},                               \
		{NULL, _l_name, libprotos, (sizeof(libprotos) / sizeof(libprotos[0]))}}; \
	POCO_EXPORT Pocorex* poco_rexlib_get(void)                                   \
	{                                                                            \
		return &rexlib_header;                                                   \
	}

#endif /* POCOREX_H */
