/*****************************************************************************
 * packcmap.c - Pack an arbitrary number of colours to a smaller palette.
 *
 * Adapted from poco/poekit/pdracc/packcmap.c (Ian's POE version of the
 * adaptive-threshold streaming colour-packing algorithm).  Moved here so
 * it can be used by both the ani host and Poco scripts without requiring
 * the pdracces.poe module.
 *
 * The Poco-visible wrapper is po_pack_colortable(), registered as
 * "PackColorTable" in the picdrive Poco library.
 ****************************************************************************/

#include <string.h>
#include <stdlib.h>

#include "errcodes.h"
#include "cmap.h"
#include "pocolib.h"
#include "pocoface.h"

extern Errcode builtin_err;
extern int closestc(const Rgb3* rgb, const Rgb3* cmap, int count);
extern int color_dif(const Rgb3* c1, const Rgb3* c2);

/*****************************************************************************
 * Do a pack with a given threshold.
 * Returns the number of new colours added, or a negative Errcode.
 ****************************************************************************/
static Errcode find_newc(int usedc, int freec, Rgb3* ctab, int threshold, Rgb3* lsource,
						 long lscount, USHORT* curthresh)
{
	int closestix;
	long new;
	int dif;

	new = 0;
	while (--lscount >= 0) {
		if (*curthresh > threshold) {
			closestix = closestc(lsource, ctab, usedc);
			dif = color_dif(ctab + closestix, lsource);
			if (dif > threshold) {
				if (new >= freec) {
					return Err_overflow;
				}
				new ++;
				ctab[usedc] = *lsource;
				usedc++;
				*curthresh = 0;
			} else {
				*curthresh = dif;
			}
		}
		++curthresh;
		++lsource;
	}

	return (Errcode) new;
}

/*****************************************************************************
 * Pack colours with adaptive threshold streaming algorithm.
 *
 * source  — array of scount Rgb3 triples (the full colour list)
 * scount  — number of entries in source
 * dest    — destination palette; must have room for at least COLORS entries
 *           even if dcount is smaller
 * dcount  — target palette size (will be packed to this many)
 *
 * Returns Success on completion, or an Errcode on failure.
 ****************************************************************************/
Errcode fpack_ctable(Rgb3* source, long scount, Rgb3* dest, int dcount)
{
	int threshold;
	int newused;
	int usedc;
	int hiwater;
	int reduction_factor;
	USHORT* curthresh = NULL;
	int ctsize = scount * sizeof(*curthresh);

	if (scount <= dcount) {
		memcpy(dest, source, scount * sizeof(Rgb3));
		return Success;
	}

	curthresh = malloc(ctsize);
	if (curthresh == NULL) {
		return Err_no_memory;
	}

	*dest = *source;
	usedc = 1;
	threshold = 1024;

	/* Do the first threshold.  If there's overflow,
	 * double the threshold and try again. */
FINDHI:
	memset(curthresh, 0xff, ctsize);
	*curthresh = 0;
	newused = find_newc(usedc, dcount - usedc, dest, threshold, source, scount, curthresh);
	if (newused == Err_overflow) {
		threshold *= 2;
		goto FINDHI;
	} else if (newused < Success) {
		goto OUT;
	}
	usedc += newused;

	/* Keep doing finer thresholds until we run out of palette slots. */
	hiwater = threshold;
	reduction_factor = 2;
	while (threshold >= 3) {
		newused = find_newc(usedc, dcount - usedc, dest, threshold, source, scount, curthresh);
		if (newused < Success) {
			if (newused != Err_overflow) {
				goto OUT;
			} else {
				newused = Success;
				threshold += threshold / 3;
				reduction_factor = 3;
				if (threshold >= hiwater) {
					goto OUT;
				}
			}
		} else {
			hiwater = threshold;
			threshold -= threshold / reduction_factor;
		}
		usedc += newused;
	}

OUT:
	free(curthresh);
	return (newused < Success) ? newused : Success;
}

/*****************************************************************************
 * Poco-callable wrapper:
 *   ErrCode PackColorTable(int *source, int source_count,
 *                          int *dest,   int dest_count);
 *
 * The source and dest arrays are flat int arrays of (r, g, b) triples
 * (3 ints per colour), matching what Poco scripts can construct easily.
 *
 * The source array has source_count*3 elements.
 * The dest array must have room for dest_count*3 elements.
 ****************************************************************************/
Errcode po_pack_colortable(int* source, int source_count, int* dest, int dest_count)
{
	Rgb3* src_rgb = NULL;
	Rgb3* dst_rgb = NULL;
	Errcode err;
	int i;

	if (source == NULL || dest == NULL) {
		return builtin_err = Err_null_ref;
	}

	if (source_count < 1 || dest_count < 1) {
		return builtin_err = Err_bad_input;
	}

	src_rgb = malloc(source_count * sizeof(Rgb3));
	dst_rgb = calloc(dest_count > 256 ? dest_count : 256, sizeof(Rgb3));
	if (src_rgb == NULL || dst_rgb == NULL) {
		err = Err_no_memory;
		goto done;
	}

	/* Convert flat int array → Rgb3 array */
	for (i = 0; i < source_count; ++i) {
		src_rgb[i].r = (UBYTE)source[i * 3 + 0];
		src_rgb[i].g = (UBYTE)source[i * 3 + 1];
		src_rgb[i].b = (UBYTE)source[i * 3 + 2];
	}

	err = fpack_ctable(src_rgb, source_count, dst_rgb, dest_count);

	/* Convert Rgb3 array → flat int array */
	for (i = 0; i < dest_count; ++i) {
		dest[i * 3 + 0] = dst_rgb[i].r;
		dest[i * 3 + 1] = dst_rgb[i].g;
		dest[i * 3 + 2] = dst_rgb[i].b;
	}

done:
	free(src_rgb);
	free(dst_rgb);
	return err;
}
