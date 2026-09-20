#ifndef POCO_STDTYPES_H
#define POCO_STDTYPES_H

/*
	Poco's DOS-era scalar spellings.  The four that cross the legacy ABI
	boundary (BYTE, UBYTE, SHORT, USHORT) are owned by
	<poco/poco_legacy_types.h> and shared with Animator; the rest are private
	to Poco and defined here.

	This file used to skip its typedefs whenever the consumer's STDTYPES_H was
	already defined, which made the meaning of ULONG depend on include order.
	It no longer does: no Animator header reaches into poco/src, and LONG and
	ULONG are gone.  Poco spelled them long/unsigned long and Animator spells
	them int32_t/uint32_t, and nothing needed either one - write the C type,
	or a fixed-width type from <stdint.h>, directly.
*/

#include <stddef.h>

#include <poco/poco_legacy_types.h>

typedef unsigned int UINT;
typedef int INT;
typedef int Boolean;

#ifndef true
#define true 1
#endif
#ifndef false
#define false 0
#endif

#endif /* POCO_STDTYPES_H */
