#include "errcodes.h"
#include "memory.h"
#include "rcel.h"

/*************************************************************************
 * Make up a raster with color map in memory.  Useful for drawing off-
 * screen or storing a copy of the display screen.	When you are done
 * free this up with a call to pj_rcel_free(*pcel);
 *
 * Parameters:
 *		Rasthdr *spec;		This structure defines the size of the
 *							memory rcel.   The following fields of
 *							this structure should be filled in prior
 *							to calling this routine:
 *								.width	- set to width of cel you want
 *								.height - set to height of cel you want
 *								.pdepth - set to 8 (# of bits per pixel)
 *								.aspect_dx - set to match display screen or 1
 *								.aspect_dy - set to match display screen or 1
 *		Rcel	**pcel; 	Returns the memory Rcel.
 *		LONG	num_colors; Number of colors in color map.	Should be
 *							COLORS or 256.
 * Returns:
 *		Success (0) if all goes well, a negative error code if not.
 *		(see errcodes.h)
 *************************************************************************/
Errcode pj_rcel_bytemap_alloc(Rasthdr *spec, Rcel **pcel, LONG num_colors)
{
	Rcel *cel;
	Errcode err;
	cel = pj_zalloc(sizeof(Rcel));
	if (cel == NULL) {
		err = Err_no_memory;
		goto error;
	}

	err = pj_rcel_bytemap_open(spec, cel, num_colors);
	if (err < Success) {
		goto error;
	}
	goto done;

error:
	pj_freez(&cel);

done:
	*pcel = cel;
	return err;
}
