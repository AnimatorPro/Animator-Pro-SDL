#include "animinfo.h"
#include "picdrive.h"
#include "pjbasics.h"


/* loads an anim_info to represent a screen */
void get_screen_ainfo(Rcel *screen, Anim_info *spec)
{
	clear_struct(spec);
	spec->num_frames = 1;
	spec->width = screen->width;
	spec->height = screen->height;
	spec->depth = screen->pdepth;
	spec->x = spec->y = 0;
	spec->aspect_dx = screen->aspect_dx;
	spec->aspect_dy = screen->aspect_dy;
	spec->millisec_per_frame = DEFAULT_AINFO_SPEED;
}
