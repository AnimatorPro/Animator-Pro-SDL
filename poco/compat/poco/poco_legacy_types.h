/*
 * poco_legacy_types.h - scalar and list types carried by the legacy Poco ABI.
 *
 * Deprecated for new embedding hosts and native modules: use <poco/poco.h>.
 * This header exists so the legacy Poco_lib/Pocorex ABI has exactly one
 * definition of the handful of DOS-era types that cross the boundary.
 *
 * Both trees used to carry their own copies in files named stdtypes.h and
 * linklist.h.  The copies never collided only because the two files happened
 * to share an include guard, so whichever tree a translation unit reached
 * first silently suppressed the other.  That is not a boundary, it is a
 * coincidence; a host that already owns these types now says so explicitly:
 *
 *   POCO_LEGACY_HOST_SCALAR_TYPES  BYTE/UBYTE/SHORT/USHORT already in scope
 *   POCO_LEGACY_HOST_LIST_TYPES    Dlnode/Dlheader already in scope
 *
 * Animator defines both, because src/inc/stdtypes.h and src/inc/linklist.h
 * own those types for the whole program - including builds configured without
 * Poco, which cannot see this header at all.  Poco core defines neither and
 * takes its definitions from here.
 *
 * ULONG and LONG are deliberately absent.  Poco spelled them unsigned long
 * and long (8 bytes on LP64) while Animator spells them uint32_t and int32_t,
 * so a single shared definition could only ever be wrong for one of the two.
 * Nothing on the legacy ABI uses either one.
 */
#ifndef POCO_LEGACY_TYPES_H
#define POCO_LEGACY_TYPES_H

#include <stdint.h>

#ifndef POCO_LEGACY_HOST_SCALAR_TYPES
typedef int8_t BYTE;
typedef uint8_t UBYTE;
typedef int16_t SHORT;
typedef uint16_t USHORT;
#endif

#ifndef POCO_LEGACY_HOST_LIST_TYPES

/* Dlnode -- double linked list node.  The tag names match Animator's so the
 * two spellings of this layout cannot drift apart unnoticed. */

typedef struct dlnode {
	struct dlnode* next; /* points to next node */
	struct dlnode* prev; /* points to previous node */
} Dlnode;

/* Dlheader -- double linked list header, a three-pointer sentinel pair */

typedef struct dl_header {
	Dlnode* head;
	Dlnode* tail; /* initialized to 0 */
	Dlnode* tails_prev;
} Dlheader;

#endif /* POCO_LEGACY_HOST_LIST_TYPES */

#endif /* POCO_LEGACY_TYPES_H */
