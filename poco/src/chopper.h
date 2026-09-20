/*
 * chopper.c - source line fetching and low-level line chopping.
 */
#ifndef POCO_CHOPPER_H
#define POCO_CHOPPER_H

#include "poco_internal.h"

char* po_get_csource_line(Poco_cb* pcb);
char* po_chop_to(char* line, char* word, char letter);
char* po_skip_space(char* line);

#endif /* POCO_CHOPPER_H */
