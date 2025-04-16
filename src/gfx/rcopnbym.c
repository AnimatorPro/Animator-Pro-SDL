#include "cmap.h"
#include "errcodes.h"
#include "rcel.h"

/* opens an Rcel for given specs given pointer to a Vdevice a NULL vdriver
 * will open a Bytemap cel */
Errcode pj_rcel_bytemap_open(Rasthdr *spec, Rcel *cel, LONG num_colors)
{
	Errcode err = pj_open_bytemap(spec, (Bytemap *)cel);

	if (err < Success) {
		goto error;
	}

	err = pj_cmap_alloc(&cel->cmap, num_colors);
	if (err < Success) {
		goto error;
	}

	return Success;

error:
	pj_close_raster(cel);
	return err;
}
