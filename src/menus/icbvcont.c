#include <stdio.h>

#include "commonst.h"
#include "errcodes.h"
#include "reqlib.h"

Errcode varg_continu_box(char* formats, char* text, va_list args, char* etext)
{
	char* ctext[2];

	/* Without a live interactive screen (headless test harness, or an error
	 * raised before the UI is initialized) the windowed text box cannot be
	 * drawn.  Report to stderr instead of dereferencing a NULL Wscreen.
	 * `formats` is only the Ftext "!%" display-style spec (see
	 * ftext_format_type); the message content is in `text`/`etext`, so
	 * plain stderr intentionally omits the styling. */
	if (icb.input_screen == NULL) {
		if (etext != NULL && etext[0] != '\0') {
			fprintf(stderr, "%s\n", etext);
		}
		if (text != NULL) {
			vfprintf(stderr, text, args);
			fputc('\n', stderr);
		}
		return Err_reported;
	}

	ctext[0] = continue_str;
	ctext[1] = NULL;
	return tboxf_choice(icb.input_screen, formats, text, args, ctext, etext);
}
