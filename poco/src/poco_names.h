/*
 * Poco's linked list of strings, used for include directories, library
 * directories, predefined symbols, and string literals.
 *
 * This was poco/src/commonst.h, a DOS-era leftover whose name and include
 * guard both collided with Animator's unrelated src/inc/commonst.h.  Animator
 * has a Names of its own in src/inc/linklist.h; the two never meet, because
 * this type is internal to the compiler and is not on the legacy ABI.
 */
#ifndef POCO_NAMES_H
#define POCO_NAMES_H

typedef struct Names {
	struct Names* next;
	char* name;
} Names;

#endif /* POCO_NAMES_H */
