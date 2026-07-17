#ifndef POCO_ARRAY_H
#define POCO_ARRAY_H

/*
 * Decode element `index` of a poco `char *arr[]` argument.
 *
 * Poco stores a script's pointer array as consecutive Popot values (pocotype.c
 * sizes any pointer as sizeof(Popot)), so a native binding that takes an INPUT
 * `char **` receives a pointer to Popot elements, not raw char*.  This returns
 * element `index`'s raw string pointer.  Output `char **` parameters that the
 * host writes back must NOT use this.
 *
 * Defined in src/pocouser.c; declared here so both the bindings and the
 * headless registration test decode arrays through the same code.
 */
char* po_array_str(char** poco_array, int index);

#endif /* POCO_ARRAY_H */
