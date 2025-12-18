#include "cmap.h"
#include "rcel.h"

/* only works if both screens are depth 1 and the same dimensions */
void pj_rcel_copy(Rcel *source, Rcel *destination)
{
	pj_blitrect(source, 0, 0, destination, 0, 0, destination->width, destination->height);
	pj_cmap_copy(source->cmap, destination->cmap);
}
