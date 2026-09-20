/*
 * poco_limits.h - the compiler's fixed sizes, build tweakables and the few
 * one-line helpers every Poco translation unit wants.
 *
 * Split out of poco_internal.h so that the type headers below it
 * (poco_typemodel.h, poco_frames.h, poco_pp_state.h) can be self-contained
 * without dragging the whole control block in.
 */
#ifndef POCO_LIMITS_H
#define POCO_LIMITS_H

#include <stddef.h>

/* STRING_EXPERIMENT selects the experimental String type (see POSTRING.C).
 * It is defined by the POCO_STRING_EXPERIMENT CMake option, not here; builds
 * outside CMake get the default-off behaviour by simply not defining it. */

#if 1
#ifndef DEVELOPMENT
#define DEVELOPMENT /* Include code to check 'cannot happen' cases. */
#endif
#endif

#ifndef VRSN_NUM
#define VRSN_NUM 184 /* this is usually defined externally via -D	*/
#endif

#ifndef stricmp
#define stricmp strcasecmp
#endif

/* Thread-local storage class, spelled for the compilers this builds under. */
#if defined(_MSC_VER)
#define POCO_THREAD_LOCAL __declspec(thread)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define POCO_THREAD_LOCAL _Thread_local
#elif defined(__GNUC__) || defined(__clang__)
#define POCO_THREAD_LOCAL __thread
#else
#define POCO_THREAD_LOCAL
#endif

/*****************************************************************************
 * miscellanious macros...
 *	 these could also be considered tweakable, but do it with care.
 ****************************************************************************/
#define max(a, b)               \
	({                          \
		__typeof__(a) _a = (a); \
		__typeof__(b) _b = (b); \
		_a > _b ? _a : _b;      \
	})

#define min(a, b)               \
	({                          \
		__typeof__(a) _a = (a); \
		__typeof__(b) _b = (b); \
		_a < _b ? _a : _b;      \
	})

#define VRSN_TO_STR(a) #a
#define VRSN_STR (VRSN_TO_STR(VRSN_NUM))

#define MAX_STACK 14336         /* 14k stack, used in overflow checking */
#define HASH_SIZE 256           /* hash table size, must change pocoutil.asm if this changes!!! */
#define SZTOKE 512              /* max line length, token length */
#define MAX_SYM_LEN 40          /* max significant chars in sym name */
#define MAX_STRLIT_LEN 4096     /* max size of one string literal */
#define SMALL_CODE_SIZE 48      /* size of small code buffer */
#define SMALLBLK_CACHE_SIZE 512 /* Used for code_buf and line_data caching. */
#define MAX_TYPE_COMPS 21

#define FUNC_MAGIC 0x27680317L /* magic number validates a function frame */

typedef char PoBoolean; /* small/fast boolean datatype for poco */

#endif /* POCO_LIMITS_H */
