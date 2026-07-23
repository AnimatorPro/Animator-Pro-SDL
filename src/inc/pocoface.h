/*
 * Compatibility-only Animator header.
 *
 * Deprecated for new Animator code: use <poco/poco.h>.  This shim preserves
 * legacy compile_poco/run_poco/free_poco source compatibility while keeping
 * their declarations owned by Poco's single compatibility header.
 */
#ifndef ANIMATOR_POCOFACE_COMPAT_H
#define ANIMATOR_POCOFACE_COMPAT_H

/* Supply Animator's Names tag at the only legacy function parameter boundary. */
#include "errcodes.h"
#include "stdtypes.h"
#include "linklist.h"
#define POCO_LEGACY_NAMES_TYPE Names
#include "../../poco/src/pocoface.h"
#undef POCO_LEGACY_NAMES_TYPE

#endif /* ANIMATOR_POCOFACE_COMPAT_H */
