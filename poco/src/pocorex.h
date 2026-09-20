/*****************************************************************************
 * POCOREX.H - Compatibility-only legacy POE module ABI.
 *
 * Deprecated for new modules: export poco_module_get from <poco/poco.h> and
 * return a PocoModuleDescriptor.  Pocorex/poco_rexlib_get remain available
 * only while existing Animator POE modules migrate.
 *
 * The declarations live in <poco/poco_legacy.h> alongside the Poco_lib family
 * they extend; this file only keeps the historical include name working.
 ****************************************************************************/

#ifndef POCOREX_H
#define POCOREX_H

#ifndef POCOLIB_H
#include "pocolib.h"
#endif

#endif /* POCOREX_H */
