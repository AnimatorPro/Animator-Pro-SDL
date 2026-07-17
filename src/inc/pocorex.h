/*
 * Compatibility-only Animator header.
 *
 * Deprecated for new POE modules: use PocoModuleDescriptor from
 * <poco/poco.h>.  Existing poco_rexlib_get modules retain their exact
 * legacy ABI through Poco's single compatibility header.
 */
#ifndef ANIMATOR_POCOREX_COMPAT_H
#define ANIMATOR_POCOREX_COMPAT_H

/* Keep legacy module callbacks in Animator's error/type domain. */
#include "errcodes.h"
#include "stdtypes.h"
#include "pocolib.h"
#include "../../poco/include/pocorex.h"

#endif /* ANIMATOR_POCOREX_COMPAT_H */

