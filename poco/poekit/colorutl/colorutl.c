/*****************************************************************************
 * COLORUTL.C - A POE module containing some color-related utilities.
 *
 *	Major POE items/features demonstrated herein:
 *
 *		- Receiving and returning values to Poco callers through raw libffi
 *		  pointers, with required spans enforced by binding contracts.
 *		- Providing a library of several functions to the Poco program.
 *		- Mixed C and ASM code implement functions.
 *
 *	This POE module implements 3 functions for Poco callers:
 *
 *		int  ColorDifference(int *pcolor1, int *pcolor2);
 *
 *			This function returns the difference between a pair of rgb
 *			colors.  The difference is the sum of the squares of the
 *			differences of each of the r,g,b components.  It's useful in
 *			determining how similar two colors are.
 *
 *
 *		int  ClosestColor(int *pcolor, int *ptab, int tabcount);
 *
 *			This function searches a table of rgb color values and returns
 *			the index of the color closest to the rgb value specified. The
 *			values in the table are rgb triplets stored as 32-bit integers.
 *			(IE, like the table returned by GetScreenColorMap().)
 *
 *
 *		void GetMenuColors(int *current, int *preferred);
 *
 *			This function returns the current and preferred menu colors.
 *			The current colors are the ones in use right now (or the ones
 *			the user saw last, if no menus/dialogs are on the Ani Pro
 *			screen right now).	The preferred colors are the set the user
 *			would like to see.	These may differ from the current colors
 *			if exact matches don't exist in the user's color palette.
 *			The current and preferred colors are each a set of 5 rgb
 *			triplets; each triplet composed of 32-bit integers.  This
 *			function is useful primary when you are constructing a new
 *			color palette programmatically.  If you have some leftover
 *			slots in the palette, you can put the user's preferred
 *			colors into them; a nice gesture.  (There is no requirement
 *			to do this when constructing a color palette; Ani Pro will
 *			do its best to use as menu colors whatever exists in any
 *			palette.)
 *
 *
 * NOTES:
 *
 *		The functions herein are defined as static if they are called
 *		only from within this module, and as global if they are exported
 *		to the Poco program.
 *
 * MAINTENANCE:
 *
 *	10/15/91	Ian Lepore
 *				Created.
 *	01/10/92	Ian
 *				Added GetMenuIndexes(), RgbToHls(), and HlsToRgb() routines.
 *				Also added init_patches_10a() to Setup_Pocorex() macro.
 ****************************************************************************/

/*----------------------------------------------------------------------------
 * include the usual header files...
 *--------------------------------------------------------------------------*/

#define PUBLIC_CODE
#include "errcodes.h"   /* host error codes (must precede pocorex.h)     */
#include "rexlib.h"     /* required for the hostlibs                     */
#include "pocorex.h"    /* required header file, also includes pocolib.h */
#include "cmap.h"       /* this one defines Rgb3 and such for us.        */


/*----------------------------------------------------------------------------
 * set up the host libraries we need...
 *--------------------------------------------------------------------------*/

// #define HLIB_TYPE_1 AA_POCOLIB	/* this one is always required in a POE */
// #include <hliblist.h>

/*****************************************************************************
* convert unsigned byte rgb triplets used internally to integer triplets.
****************************************************************************/
static void rgb_to_irgb(void *irgb, void *rgb, int count)
{
	unsigned int  *pout = irgb;
	unsigned char *pin	= rgb;

	while (--count >= 0) {
		*pout++ = *pin++;
		*pout++ = *pin++;
		*pout++ = *pin++;
	}
}

/* Poco exposes each RGB component as an int, unlike Animator's packed Rgb3. */
static int color_difference_values(const int *c1, const int *c2)
{
	int dr = c1[0] - c2[0];
	int dg = c1[1] - c2[1];
	int db = c1[2] - c2[2];

	return dr*dr + dg*dg + db*db;
}

static int closest_color_index(const int *rgb, const int *table, int count)
{
	int index;
	int best = 0;
	int best_difference = 0x7fffffff;

	for (index = 0; index < count; ++index) {
		int difference = color_difference_values(rgb, &table[index * 3]);

		if (difference < best_difference) {
			best_difference = difference;
			best = index;
			if (difference == 0)
				break;
		}
	}
	return best;
}

/*****************************************************************************
* find the difference between two rgb colors.
*
*	The binding contract validates both three-component input spans before
*	libffi calls this raw-pointer implementation.
****************************************************************************/
int safe_color_dif(const int *pcolor1, const int *pcolor2)
{
	return color_difference_values(pcolor1, pcolor2);
}

/*****************************************************************************
* find the rgb color in a table that is closest to the requested color.
*
*	The binding contract validates the query and tabcount-sized table spans
*	before libffi calls this raw-pointer implementation.
****************************************************************************/
int safe_closestc(const int *pcolor, const int *ptab, int tabcount)
{
	if (tabcount < 0)
		return builtin_err = Err_parameter_range;

	return closest_color_index(pcolor, ptab, tabcount);

}

/*****************************************************************************
* return current menu rgb colors and the user's preferred menu rgb colors.
*
*	either pointer may be NULL, indicating that the caller doesn't want
*	that set of colors returned.  (both could be NULL, but that would be
*	pretty pointless, huh?)
****************************************************************************/
void menu_colors(void *current, void *preferred)
{
	Rgb3	*pl_currents;
	Rgb3	*pl_preferreds;

	GetMenuColors(NULL, &pl_currents, &pl_preferreds);

	if (current != NULL) {
		rgb_to_irgb(current, pl_currents, 5);
	}

	if (preferred != NULL) {
		rgb_to_irgb(preferred, pl_preferreds, 5);
	}

	return;
}

/*****************************************************************************
* return current menu color indexes.
*
*	the indexes (whatever happened to the word indicies anyway?) are the
*	five slots in the color palette which are currently being used for the
*	menu colors.  in other words, if you used each of the five indexes to
*	go look in the color palette, you'd find the rgb values returned by
*	the 'currents' portion of the function above.
****************************************************************************/
void menu_indexes(int *pindexes)
{
	int 	i;
	Pixel	*indexes;
	int 	*pret;

	GetMenuColors(&indexes, NULL, NULL);

	pret = pindexes;
	for (i = 0; i < 5; ++i)
		*pret++ = *indexes++;

	return;
}

/*****************************************************************************
* The binding contract validates the three writable integer outputs.
****************************************************************************/
void safe_rgb2hls(int r, int g, int b, int *ph, int *pl, int *ps)
{

	r &= 0x00FF;	/* force color components to be in 0-255 range */
	g &= 0x00FF;
	b &= 0x00FF;

	rgb_to_hls(r, g, b, ph, pl, ps);

	return;
}

/*****************************************************************************
* The binding contract validates the three writable integer outputs.
****************************************************************************/
void safe_hls2rgb(int *pr, int *pg, int *pb, int h, int l, int s)
{

	h &= 0x00FF;	/* force color components to be in 0-255 range */
	l &= 0x00FF;
	s &= 0x00FF;

	hls_to_rgb(pr, pg, pb, h, l, s);

	return;
}

/*----------------------------------------------------------------------------
 * Setup rexlib/pocorex interface structures...
 *--------------------------------------------------------------------------*/

static const PocoBindingPointerContract color_difference_spans[] = {
  { 0, POCO_POINTER_PERMISSION_READ, 1, POCO_BINDING_SPAN_BYTES,
    3*sizeof(int), POCO_BINDING_PARAMETER_NONE, POCO_BINDING_PARAMETER_NONE, POCO_BINDING_PARAMETER_NONE },
  { 1, POCO_POINTER_PERMISSION_READ, 1, POCO_BINDING_SPAN_BYTES,
    3*sizeof(int), POCO_BINDING_PARAMETER_NONE, POCO_BINDING_PARAMETER_NONE, POCO_BINDING_PARAMETER_NONE },
};
static const PocoBindingPointerContract closest_color_spans[] = {
  { 0, POCO_POINTER_PERMISSION_READ, 1, POCO_BINDING_SPAN_BYTES,
    3*sizeof(int), POCO_BINDING_PARAMETER_NONE, POCO_BINDING_PARAMETER_NONE, POCO_BINDING_PARAMETER_NONE },
  { 1, POCO_POINTER_PERMISSION_READ, 1, POCO_BINDING_SPAN_BYTES,
    3*sizeof(int), 2, POCO_BINDING_PARAMETER_NONE, POCO_BINDING_PARAMETER_NONE },
};
static const PocoBindingPointerContract menu_indexes_spans[] = {
  { 0, POCO_POINTER_PERMISSION_WRITE, 1, POCO_BINDING_SPAN_BYTES,
    5*sizeof(int), POCO_BINDING_PARAMETER_NONE, POCO_BINDING_PARAMETER_NONE, POCO_BINDING_PARAMETER_NONE },
};
static const PocoBindingPointerContract rgb_to_hls_spans[] = {
  { 3, POCO_POINTER_PERMISSION_WRITE, 1, POCO_BINDING_SPAN_BYTES,
    sizeof(int), POCO_BINDING_PARAMETER_NONE, POCO_BINDING_PARAMETER_NONE, POCO_BINDING_PARAMETER_NONE },
  { 4, POCO_POINTER_PERMISSION_WRITE, 1, POCO_BINDING_SPAN_BYTES,
    sizeof(int), POCO_BINDING_PARAMETER_NONE, POCO_BINDING_PARAMETER_NONE, POCO_BINDING_PARAMETER_NONE },
  { 5, POCO_POINTER_PERMISSION_WRITE, 1, POCO_BINDING_SPAN_BYTES,
    sizeof(int), POCO_BINDING_PARAMETER_NONE, POCO_BINDING_PARAMETER_NONE, POCO_BINDING_PARAMETER_NONE },
};
static const PocoBindingPointerContract hls_to_rgb_spans[] = {
  { 0, POCO_POINTER_PERMISSION_WRITE, 1, POCO_BINDING_SPAN_BYTES,
    sizeof(int), POCO_BINDING_PARAMETER_NONE, POCO_BINDING_PARAMETER_NONE, POCO_BINDING_PARAMETER_NONE },
  { 1, POCO_POINTER_PERMISSION_WRITE, 1, POCO_BINDING_SPAN_BYTES,
    sizeof(int), POCO_BINDING_PARAMETER_NONE, POCO_BINDING_PARAMETER_NONE, POCO_BINDING_PARAMETER_NONE },
  { 2, POCO_POINTER_PERMISSION_WRITE, 1, POCO_BINDING_SPAN_BYTES,
    sizeof(int), POCO_BINDING_PARAMETER_NONE, POCO_BINDING_PARAMETER_NONE, POCO_BINDING_PARAMETER_NONE },
};
static const PocoBindingContract color_difference_contract = {
  color_difference_spans, Array_els(color_difference_spans), {0}
};
static const PocoBindingContract closest_color_contract = {
  closest_color_spans, Array_els(closest_color_spans), {0}
};
static const PocoBindingContract menu_indexes_contract = {
  menu_indexes_spans, Array_els(menu_indexes_spans), {0}
};
static const PocoBindingContract rgb_to_hls_contract = {
  rgb_to_hls_spans, Array_els(rgb_to_hls_spans), {0}
};
static const PocoBindingContract hls_to_rgb_contract = {
  hls_to_rgb_spans, Array_els(hls_to_rgb_spans), {0}
};

static Lib_proto poe_calls[] = {
  { safe_color_dif, "int  ColorDifference(int *pcolor1, int *pcolor2);", &color_difference_contract },
  { safe_closestc,	"int  ClosestColor(int *pcolor, int *ptab, int tabcount);", &closest_color_contract },
  { menu_colors,	"void GetMenuRGB(int *current, int *preferred);" },
  { menu_indexes,	"void GetMenuIndexes(int *indexes);", &menu_indexes_contract },
  { safe_rgb2hls,	"void RgbToHls(int r, int g, int b, int *h, int *l, int *s);", &rgb_to_hls_contract},
  { safe_hls2rgb,	"void HlsToRgb(int *r, int *g, int *b, int h, int l, int s);", &hls_to_rgb_contract},
};

Setup_Pocorex(NOFUNC, NOFUNC, "Color Utilities v1.1", poe_calls);
