/*
 * Poco's linked list of strings, used for include directories, library
 * directories and predefined symbols, and the list of a program's string
 * literals.
 *
 * This was poco/src/commonst.h, a DOS-era leftover whose name and include
 * guard both collided with Animator's unrelated src/inc/commonst.h.  Animator
 * has a Names of its own in src/inc/linklist.h; the two never meet, because
 * this type is internal to the compiler and is not on the legacy ABI.
 */
#ifndef POCO_NAMES_H
#define POCO_NAMES_H

#include <stddef.h>

typedef struct Names {
	struct Names* next;
	char* name;
} Names;

/*
 * A string literal.  length counts its bytes, not the NUL stored after them;
 * strlen() stops short when the literal contains "\0".  next is first so
 * po_freelist() can free the list.
 */
typedef struct PoLiteral {
	struct PoLiteral* next;
	char* text;
	size_t length;
} PoLiteral;

#endif /* POCO_NAMES_H */
