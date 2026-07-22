#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include "port.h"

void* poco_port_malloc(size_t size)
{
	return malloc(size);
}

void* poco_port_zalloc(size_t size)
{
	void* pointer = poco_port_malloc(size);

	if (pointer != NULL) {
		memset(pointer, 0, size);
	}
	return pointer;
}

void poco_port_free(void* pointer)
{
	free(pointer);
}

void poco_port_gentle_free(void* pointer)
{
	if (pointer != NULL) {
		poco_port_free(pointer);
	}
}

void poco_port_freez(void* pointer_to_pointer)
{
	void** pointer = pointer_to_pointer;

	if (pointer != NULL) {
		poco_port_gentle_free(*pointer);
		*pointer = NULL;
	}
}

int poco_port_delete(const char* name)
{
	return remove(name);
}

Errcode poco_port_ioerr(void)
{
	return Success;
}

void poco_port_upc(char* text)
{
	if (text == NULL) {
		return;
	}
	for (; *text; ++text) {
		*text = (char)toupper((unsigned char)*text);
	}
}

char* poco_port_clone_string(const char* text)
{
	char* copy;
	size_t length;

	if (text == NULL) {
		return NULL;
	}
	length = strlen(text) + 1;
	copy = poco_port_malloc(length);
	if (copy != NULL) {
		memcpy(copy, text, length);
	}
	return copy;
}

void poco_port_init_stdfiles(void)
{
}

void poco_port_cleanup_lfiles(void)
{
}
