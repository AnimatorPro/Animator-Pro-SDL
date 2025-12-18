#define PENCEL_C

/* pencel.c - Routines to maintain a Rcel type structure. of the current
 * vb.pencel size and shape a pencel is not garanteed to be a Ramrast */

#include "errcodes.h"
#include "jimk.h"
#include "pentools.h"

/* allocates a pencel (Rcel) the size of the vb.pencel */
Errcode alloc_pencel(Rcel **pcel)
{
	return valloc_ramcel(pcel, vb.pencel->width, vb.pencel->height);
}

/* only works if rcels are of same dimensions and specs */
void swap_pencels(Rcel *source, Rcel *destination)
{
	pj_swaprect(source, 0, 0, destination, 0, 0, destination->width, destination->height);
	swap_cmaps(source->cmap, destination->cmap);
}

/* get copy of an rcel in memory */
Rcel *clone_pencel(Rcel *s)
{
	Rcel *result;

	if (alloc_pencel(&result) >= Success) {
		pj_rcel_copy(s, result);
	}
	return result;
}
