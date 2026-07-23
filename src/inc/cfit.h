#ifndef __CFIT_H
#define __CFIT_H

#ifndef CMAP_H
#include "cmap.h"
#endif

#ifndef STDTYPES_H
#include "stdtypes.h"
#endif

extern void make_cfit_table(Rgb3* scm, Rgb3* dcm, Pixel* cnums, int clearc);

#endif  // __CFIT_H
