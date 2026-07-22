//
// Created by Charles Wardlaw on 2024-04-06.
//

#ifndef ANIMATOR_PRO_POCODRAW_H
#define ANIMATOR_PRO_POCODRAW_H

#include "stdtypes.h"

void po_ink_line(int x1, int y1, int x2, int y2);
void po_hls_to_rgb(int h, int l, int s, int* r, int* g, int* b);
void po_rgb_to_hls(int r, int g, int b, int* h, int* l, int* s);
int po_closest_color_in_screen(void* screen, int r, int g, int b);
ErrCode po_squeeze_colors(int* source_map, int source_count, int* dest_map, int dest_count);
Errcode po_fit_screen_to_color_map(void* screen, int* new_colors, bool keep_key);


#endif  // ANIMATOR_PRO_POCODRAW_H
