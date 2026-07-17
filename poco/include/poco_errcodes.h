#ifndef POCO_ERRCODES_H
#define POCO_ERRCODES_H

#ifdef ERRCODES_H
/* Host context: Errcode and common error codes already defined.
 * Only add poco-specific codes not in the host header. */

#ifndef Err_poco_ffi_func_not_found
#define Err_poco_ffi_func_not_found  (-1201)
#define Err_poco_ffi_no_protos       (-1202)
#define Err_poco_ffi_no_func_map     (-1203)
#define Err_poco_ffi_no_map_insert   (-1204)
#define Err_poco_ffi_variadic_overflow (-1205)
#define Err_poco_ffi_invalid_binding (-1206)
#define Err_poco_ffi_bounds          (-1207)
#endif

#ifndef Err_poco_lib_not_found
#define Err_poco_lib_not_found   (-1300)
#define Err_poco_lib_load_failed (-1301)
#define Err_poco_lib_no_entry    (-1302)
#define Err_poco_lib_invalid     (-1303)
#define Err_poco_lib_version     (-1304)
#define Err_poco_lib_empty       (-1305)
#endif

#else /* Standalone poco: full enum unchanged */

typedef enum Errcode {
	Success = 0,
	Err_reported = -1,
	Err_null_ref = -2,
	Err_invalid_FILE = -3,
	Err_buf_too_small = -4,
	Err_no_memory = -5,
	Err_string = -6,
	Err_parameter_range = -7,
	Err_too_few_params = -8,
	Err_too_many_params = -9,
	Err_create = -10,
	Err_in_err_file = -11,
	Err_not_found = -12,
	Err_no_main = -13,
	Err_poco_internal = -14,
	Err_free_null = -15,
	Err_poco_free = -16,
	Err_write = -17,
	/* Additional Animator Pro codes used by poco */
	Err_unimpl = -18,
	Err_overflow = -19,
	Err_no_file = -100,
	Err_no_device = -102,
	Err_read = -104,
	Err_dir_too_long = -143,
	Err_dir_name_err = -144,
	Err_file_name_err = -145,
	Err_seek = -105,
	Err_eof = -106,
	Err_access = -118,
	Err_stack = -402,
	Err_bad_instruction = -403,
	Err_syntax = -404,
	Err_zero_divide = -411,
	Err_float = -412,
	Err_index_small = -414,
	Err_index_big = -415,
	Err_free_resources = -418,
	Err_zero_malloc = -419,
	Err_fread_buf = -421,
	Err_fwrite_buf = -422,
	Err_early_exit = -425,
	Err_function_not_found = -427,
	Err_poco_exit = -429,
	Err_abort = -430,
	Err_not_implemented = -551,
	/* poco FFI specific */
	Err_poco_ffi_func_not_found = -1201,
	Err_poco_ffi_no_protos = -1202,
	Err_poco_ffi_no_func_map = -1203,
	Err_poco_ffi_no_map_insert = -1204,
	Err_poco_ffi_variadic_overflow = -1205,
	Err_poco_ffi_invalid_binding = -1206,
	Err_poco_ffi_bounds = -1207,
	/* poco library loading errors */
	Err_poco_lib_not_found = -1300,
	Err_poco_lib_load_failed = -1301,
	Err_poco_lib_no_entry = -1302,
	Err_poco_lib_invalid = -1303,
	Err_poco_lib_version = -1304,
	Err_poco_lib_empty = -1305
} Errcode;

#define ERRTEXT_SIZE 256

#endif

#endif /* POCO_ERRCODES_H */
