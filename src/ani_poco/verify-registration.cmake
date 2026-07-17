if(NOT DEFINED ANI_SOURCE_DIR)
	message(FATAL_ERROR "ANI_SOURCE_DIR is required")
endif()

set(REGISTRY_SOURCE "${ANI_SOURCE_DIR}/pocolibs.c")
set(RUNNER_SOURCE "${ANI_SOURCE_DIR}/qpoco.c")

foreach(required_file REGISTRY_SOURCE RUNNER_SOURCE)
	if(NOT EXISTS "${${required_file}}")
		message(FATAL_ERROR "Missing Animator Poco registration input: ${${required_file}}")
	endif()
endforeach()

file(READ "${REGISTRY_SOURCE}" registry_source)
file(READ "${RUNNER_SOURCE}" runner_source)

foreach(required_library
	po_user_lib po_draw_lib po_text_lib po_mode_lib po_turtle_lib po_time_lib
	po_cel_lib po_alt_lib po_optics_lib po_blit_lib po_misc_lib po_load_save_lib
	po_FILE_lib po_str_lib po_mem_lib po_math_lib po_dos_lib po_globalv_lib
	po_title_lib po_tween_lib po_flicplay_lib po_picdrive_lib)
	if(NOT registry_source MATCHES "&${required_library}")
		message(FATAL_ERROR
			"Animator binding registry must explicitly install ${required_library}")
	endif()
endforeach()

if(NOT registry_source MATCHES "PocoStatus ani_poco_register_libraries" OR
	NOT registry_source MATCHES "poco_vm_register_library")
	message(FATAL_ERROR
		"Animator bindings must be installed through the public Poco library API")
endif()

if(registry_source MATCHES "get_poco_libs")
	message(FATAL_ERROR "The Animator adapter must not rebuild a legacy get_poco_libs chain")
endif()

foreach(required_vm_call
	poco_vm_create ani_poco_register_libraries poco_vm_compile_file poco_vm_run
	poco_program_destroy poco_vm_destroy)
	if(NOT runner_source MATCHES "${required_vm_call}")
		message(FATAL_ERROR
			"Animator runner must use ${required_vm_call} through the adapter")
	endif()
endforeach()

foreach(forbidden_legacy_call compile_poco run_poco free_poco get_poco_libs)
	if(runner_source MATCHES "(^|[^A-Za-z0-9_])${forbidden_legacy_call}[ \\t\\r\\n]*\\(")
		message(FATAL_ERROR
			"Animator runner must not directly call ${forbidden_legacy_call}")
	endif()
endforeach()
