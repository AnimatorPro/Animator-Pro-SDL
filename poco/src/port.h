/* Poco-private portability runtime.  Do not install this header. */
#ifndef POCO_PRIVATE_PORT_H
#define POCO_PRIVATE_PORT_H

#include <stddef.h>

#include "poco_errcodes.h"

void* poco_port_malloc(size_t size);
void* poco_port_zalloc(size_t size);
void poco_port_free(void* pointer);
void poco_port_gentle_free(void* pointer);
void poco_port_freez(void* pointer_to_pointer);

int poco_port_delete(const char* name);
Errcode poco_port_ioerr(void);

void poco_port_upc(char* text);
char* poco_port_clone_string(const char* text);

void poco_port_init_stdfiles(void);
void poco_port_cleanup_lfiles(void);

/*
 * Keep the compiler sources readable while binding every legacy portability
 * call to Poco-owned symbols.  These aliases are intentionally private so an
 * Animator host's pj_* family can remain linked alongside Poco.
 */
#define pj_malloc poco_port_malloc
#define pj_zalloc poco_port_zalloc
#define pj_free poco_port_free
#define pj_gentle_free poco_port_gentle_free
#define pj_freez poco_port_freez
#define pj_delete poco_port_delete
#define pj_ioerr poco_port_ioerr
#define upc poco_port_upc
#define clone_string poco_port_clone_string
#define init_stdfiles poco_port_init_stdfiles
#define cleanup_lfiles poco_port_cleanup_lfiles

#endif /* POCO_PRIVATE_PORT_H */
