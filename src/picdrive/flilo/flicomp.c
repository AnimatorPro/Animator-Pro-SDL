#include <string.h>
#include "cmap.h"
#include "flilo.h"
#include "ptrmacro.h"
#include "rastcomp.h"

static void shift_copy_cmap(const Rgb3 *src, Rgb3 *dst, unsigned int size)
{
	unsigned int i;

	for (i = 0; i < size; i++) {
		dst[i].r = (src[i].r >> 2);
		dst[i].g = (src[i].g >> 2);
		dst[i].b = (src[i].b >> 2);
	}
}
static char *full_cmap(void *cbuf,Rgb3 *ctab)
{
register UBYTE *bbuf = cbuf;

	*bbuf++ = 1;
	*bbuf++ = 0;
	*bbuf++ = 0;
	*bbuf++ = 0;
	shift_copy_cmap(ctab, (Rgb3 *)bbuf, COLORS);
	return(norm_pointer(bbuf+COLORS));
}
static void *comp_cmap(Rgb3 *last_ctab, Rgb3 *this_ctab, void *cbuf)
{
UBYTE *last, *this;

	last = ((UBYTE*)cbuf) + (COLORS*6);
	this = last + (COLORS*6);
	shift_copy_cmap(last_ctab, (Rgb3 *)last, COLORS);
	shift_copy_cmap(this_ctab, (Rgb3 *)this, COLORS);
	return(pj_fccomp((Rgb3 *)last, (Rgb3 *)this, cbuf, COLORS));
}

static LONG flow_comp_rect(void *comp_buf,
					Rcel *last_screen,
					Rcel *this_screen, Rectangle *rect,
						   bool do_colors, SHORT type)

/* given two screens, or one in the case of the types FLI_BRUN and FLI_COLOR_0
 * compresses the delta between them or the data in the one and puts
 * resulting record in *comp_buf, 
 * returns length of compressed buffer including a prepended and loaded
 * Chunk_id this does not clip the rectangle with the screen and rectangle
 * must be within both screens provided */
{
void *vp;
Fli_frame *frame;
Chunk_id *chunk;

	frame = (Fli_frame *)comp_buf;
	memset(frame, 0, sizeof(*frame));
	chunk = (Chunk_id *)(frame+1);

	if(do_colors)
	{
		/* if requested make the color map chunk */

		if(type == FLI_BRUN || type == FLI_COLOR_0)
			vp = full_cmap((char *)(chunk+1), this_screen->cmap->ctab);
		else
		{
			if(last_screen == NULL) /* protection from bad input */
				return(0);

			vp = comp_cmap(last_screen->cmap->ctab, 
						   this_screen->cmap->ctab, chunk+1);
		}

		chunk->type = FLI_COLOR;
		chunk->size = SIZE(chunk,vp);

		if (chunk->size == EMPTY_DCOMP)
			vp = chunk;
		else
		{
			chunk = vp;
			frame->chunks = 1;
		}
	}
	else /* no color chunk requested */
	{
		vp = chunk;
	}

	switch(type)
	{
		case FLI_LC:
			vp = pj_lccomp_rects(
					(Raster *)last_screen, chunk+1, rect->x, rect->y,
					(Raster *)this_screen, rect->x, rect->y,
					rect->width, rect->height);
			break;
		case FLI_BRUN:
			vp = flow_brun_rect((Raster *)this_screen, chunk+1,
					  	   rect->x,rect->y,rect->width,rect->height);
			break;
	}

	if(vp == NULL)
	{
		vp = chunk+1;
		pj_get_rectpix(this_screen,vp,
					rect->x,rect->y,rect->width,rect->height);
		chunk->type = FLI_COPY;
		chunk->size = rect->width * rect->height;
		vp = FOPTR(vp,chunk->size);
		chunk->size += sizeof(chunk);
	}
	else
	{
		chunk->type = type;
		chunk->size = SIZE(chunk,vp);
	}

	chunk->size += chunk->size & 1;	/* force even allignment */

	if (chunk->size == EMPTY_DCOMP)
		vp = chunk;
	else
		++frame->chunks;

	frame->type = FCID_FRAME;
	frame->size = SIZE(comp_buf,vp);
	return(frame->size);
}
LONG flow_comp_cel(void *comp_buf,
				  Rcel *last_screen,
				  Rcel *this_screen, SHORT type)

/* shell around fli_comp_rect() that uses the size of the screen as the 
 * rectangle to compress. assumes both screens the same size */
{
Rectangle rect;

	rect.x = 0;
	rect.y = 0;
	rect.width = this_screen->width;
	rect.height = this_screen->height;

	return(flow_comp_rect(comp_buf,last_screen,this_screen,&rect, true,type));
}
