/* Consolidated, single-guard version */
#ifndef PTRMACRO_H
#define PTRMACRO_H

#include "stdtypes.h"

#define OPTR(base, offset) ((void*)((char*)(base) + (offset)))

#endif /* PTRMACRO_H */
