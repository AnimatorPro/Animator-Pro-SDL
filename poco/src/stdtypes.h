#ifndef POCO_STDTYPES_H
#define POCO_STDTYPES_H

/*
	Unfortunately, there're two copies of an "stdtypes.h" file
	in the project.  This is likely related to the splitting
	of the poco library from the main project; it can be fixed
	a a later date by converting all these defines into regular
	C standard types, using stdint.h and stdbool.h.
*/


/* If the main project's stdtypes has already been included, avoid redefining. */
#ifndef STDTYPES_H
#include <stddef.h>

typedef unsigned char UBYTE;
typedef signed char BYTE;
typedef unsigned short USHORT;
typedef short SHORT;
typedef unsigned int UINT;
typedef int INT;
typedef unsigned long ULONG;
typedef long LONG;
typedef int Boolean;

#ifndef true
#define true 1
#endif
#ifndef false
#define false 0
#endif

#endif /* STDTYPES_H */

#endif /* POCO_STDTYPES_H */
