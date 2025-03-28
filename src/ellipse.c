/* Ellipse.c - draw an ellipse possibly with tilted axis's.  The jaggies
   on this one are still kind of ugly.  Keep promising myself to improve
   it someday. */

#include "jimk.h"
#include "errcodes.h"
#include "marqi.h"
#include "pentools.h"
#include "poly.h"

static int ell_points(int bothrad)
{
register int p2 = 8;

for (;;)
	{
	if (p2 > bothrad)
		return(p2);
	p2 += p2;
	}
}

Errcode oval_loop(Poly *poly, Pixel color,
	int *pxrad, int *pyrad, int *ptheta)
{
Errcode err;
int bk;
SHORT diam;
Short_xy cent;
Marqihdr mh;
int theta_offset;
int xrad,yrad;


	if((err = rub_circle_diagonal(&cent,&diam,color)) < 0 )
		goto OUT;

	cinit_marqihdr(&mh,color,color, true);
	xrad = yrad = (diam+1)>>1;
	theta_offset = 0;
	bk = 0;
	for (;;)
	{
		theta_offset = -arctan(icb.mx - cent.x, icb.my - cent.y);
		xrad = calc_distance(cent.x,cent.y,icb.mx,icb.my);
		make_sp_wpoly(poly,cent.x,cent.y,
					  xrad,theta_offset, ell_points((xrad+yrad)/2),
					  WP_ELLIPSE, yrad);
		if(bk)
			break;
		marqi_poly(&mh,poly, true);
		wait_input(MBPEN|MBRIGHT|MMOVE);
		undo_poly(&mh,poly, true);
		if(JSTHIT(MBPEN|MBRIGHT|KEYHIT))
		{
			if (JSTHIT(MBRIGHT|KEYHIT))
				err = Err_abort;
			bk = 1;
		}
	}
OUT:
	*ptheta = theta_offset;
	*pxrad = xrad;
	*pyrad = yrad;
	return(err);
}

Errcode ovalf_tool(Pentool *pt, Wndo *w)
{
	Errcode err;
	int rad;
	int x,y;
	(void)pt;
	(void)w;

	if (!pti_input())
		return(Success);
	save_undo();
	if((err = oval_loop(&working_poly, vs.ccolor,&x,&y,&rad)) >= Success)
		err = maybe_finish_polyt(vs.fillp, true);
	return(err);
}

