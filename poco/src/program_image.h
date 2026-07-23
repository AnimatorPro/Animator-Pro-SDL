/* Private program-image payload codec layered inside bytecode_container. */
#ifndef POCO_PROGRAM_IMAGE_H
#define POCO_PROGRAM_IMAGE_H

#include <stddef.h>
#include <stdint.h>

#include "poco/poco.h"

typedef enum PoProgramImageStatus {
	PO_PROGRAM_IMAGE_OK = 0,
	PO_PROGRAM_IMAGE_INVALID_ARGUMENT,
	PO_PROGRAM_IMAGE_OUT_OF_MEMORY,
	PO_PROGRAM_IMAGE_TRUNCATED,
	PO_PROGRAM_IMAGE_VERSION_MISMATCH,
	PO_PROGRAM_IMAGE_MALFORMED,
	PO_PROGRAM_IMAGE_UNSUPPORTED,
	PO_PROGRAM_IMAGE_UNKNOWN_BINDING,
	PO_PROGRAM_IMAGE_MODULE_NOT_FOUND,
	PO_PROGRAM_IMAGE_MODULE_LOAD_FAILED,
	PO_PROGRAM_IMAGE_MODULE_NO_ENTRY,
	PO_PROGRAM_IMAGE_MODULE_INVALID,
	PO_PROGRAM_IMAGE_MODULE_VERSION,
	PO_PROGRAM_IMAGE_MODULE_EMPTY,
	PO_PROGRAM_IMAGE_EXTERNAL_LIBRARIES_DISABLED
} PoProgramImageStatus;

enum {
	PO_PROGRAM_IMAGE_SECTION_CODE = 1,
	PO_PROGRAM_IMAGE_SECTION_CONSTANTS = 2,
	PO_PROGRAM_IMAGE_SECTION_PROTOTYPES = 3,
	PO_PROGRAM_IMAGE_SECTION_SYMBOLS = 4,
	PO_PROGRAM_IMAGE_SECTION_STRUCT_DEFINITIONS = 5,
	PO_PROGRAM_IMAGE_SECTION_DEBUG_LOCALS = 6,
	PO_PROGRAM_IMAGE_SECTION_EXTERNAL_LIBRARIES = 7
};

PoProgramImageStatus po_program_image_encode(const PocoProgram* program, PocoDebugLevel debug_level,
											 uint8_t** out_image, size_t* out_image_size);
PoProgramImageStatus po_program_image_decode(PocoVm* vm, const void* image, size_t image_size,
											 PocoDebugLevel debug_level, const char* script_path,
											 PocoProgram** out_program);

#endif /* POCO_PROGRAM_IMAGE_H */
