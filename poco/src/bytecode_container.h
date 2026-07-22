#ifndef POCO_BYTECODE_CONTAINER_H
#define POCO_BYTECODE_CONTAINER_H

#include "poco_hash.h"

#include <stddef.h>
#include <stdint.h>

/*
 * Serialized bytecode is a strict-versioned internal format. Public file and
 * memory entry points are layered over this codec.
 */
#define POCO_BYTECODE_FORMAT_VERSION 2u
#define POCO_BYTECODE_MAGIC_SIZE 8u
#define POCO_BYTECODE_HEADER_SIZE 120u

typedef enum PoBytecodeContainerStatus {
	PO_BYTECODE_CONTAINER_OK = 0,
	PO_BYTECODE_CONTAINER_INVALID_ARGUMENT,
	PO_BYTECODE_CONTAINER_OUT_OF_MEMORY,
	PO_BYTECODE_CONTAINER_TRUNCATED,
	PO_BYTECODE_CONTAINER_BAD_MAGIC,
	PO_BYTECODE_CONTAINER_VERSION_MISMATCH,
	PO_BYTECODE_CONTAINER_BAD_LAYOUT,
	PO_BYTECODE_CONTAINER_INTEGRITY_MISMATCH,
	PO_BYTECODE_CONTAINER_SOURCE_MISMATCH
} PoBytecodeContainerStatus;

typedef enum PoBytecodeDebugLevel {
	PO_BYTECODE_DEBUG_MINIMAL = 0,
	PO_BYTECODE_DEBUG_EXTENDED = 1
} PoBytecodeDebugLevel;

/*
 * How the program image encodes opcode<->line and live-range endpoints.
 * Format version 2 always emits portable instruction ordinals (byte offsets
 * into target-native code are not stable across ABIs); the field is recorded
 * for self-description and forward flexibility.
 */
typedef enum PoBytecodeOffsetRepr {
	PO_BYTECODE_OFFSET_REPR_NATIVE = 0,
	PO_BYTECODE_OFFSET_REPR_ORDINAL = 1
} PoBytecodeOffsetRepr;

/*
 * Self-describing record of the ABI of the host that produced the container.
 * The serialized program image is host-independent — integers use a fixed
 * little-endian codec, the opcode stream is canonical, debug offsets are
 * instruction ordinals, and struct layout is recomputed on load — so this
 * block is informational and validated for internal consistency rather than
 * required to match the loading host.
 */
typedef struct PoBytecodeAbi {
	uint8_t endianness; /* 0 = little, 1 = big */
	uint8_t pointer_size;
	uint8_t int_size;
	uint8_t long_size;
	uint8_t double_size;
	uint8_t max_align;
	uint8_t offset_repr; /* PoBytecodeOffsetRepr */
} PoBytecodeAbi;

/*
 * A validated borrowed view. image remains valid only while its serialized
 * container bytes remain alive and unchanged.
 */
typedef struct PoBytecodeEnvelopeView {
	const uint8_t* image;
	size_t image_size;
	PoBytecodeDebugLevel debug_level;
	uint8_t source_hash[POCO_BLAKE3_HASH_SIZE];
	PoBytecodeAbi abi;
} PoBytecodeEnvelopeView;

/*
 * Allocate and encode one canonical envelope. The caller owns *out_container
 * and releases it with free(). Source bytes are hashed into the content
 * address; image bytes are independently hashed for container integrity.
 */
PoBytecodeContainerStatus po_bytecode_envelope_write(const void* source, size_t source_size,
													 const void* image, size_t image_size,
													 PoBytecodeDebugLevel debug_level,
													 uint8_t** out_container,
													 size_t* out_container_size);

/*
 * Allocate an envelope using an already-computed source content address. This
 * is used by source-less serialized programs, which retain the address but not
 * the bytes from which it was computed.
 */
PoBytecodeContainerStatus po_bytecode_envelope_write_hash(
	const uint8_t source_hash[POCO_BLAKE3_HASH_SIZE], const void* image, size_t image_size,
	PoBytecodeDebugLevel debug_level, uint8_t** out_container, size_t* out_container_size);

/* Validate framing, strict version, layout, and image integrity. */
PoBytecodeContainerStatus po_bytecode_envelope_read(const void* container, size_t container_size,
													PoBytecodeEnvelopeView* out_view);

/* Verify source bytes against the content address copied into a read view. */
PoBytecodeContainerStatus po_bytecode_envelope_verify_source(const PoBytecodeEnvelopeView* view,
															 const void* source,
															 size_t source_size);

#endif
