cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(ani C)

if(APPLE)
	enable_language(OBJC)
endif()

set(CMAKE_CXX_STANDARD 17)

include_directories("inc")
if (WIN32)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /EHsc")
else()
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -W -Wall -Wextra -Wmissing-prototypes")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wold-style-definition")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-sign-compare")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-pointer-sign")
endif()

add_subdirectory(fileio)
add_subdirectory(fli)
add_subdirectory(font)
add_subdirectory(gfx)
add_subdirectory(input)
add_subdirectory(menus)
add_subdirectory(picdrive)
add_subdirectory(pjhigh)
add_subdirectory(pj_sdl)
add_subdirectory(rastlib)
add_subdirectory(rexlib)
add_subdirectory(softmenu)
add_subdirectory(util)
add_subdirectory(wndo)

if (WITH_POCO AND WITH_ANI)
	add_subdirectory(ani_poco)

	add_test(
		NAME ani_poco_adapter_target
		COMMAND ${CMAKE_COMMAND}
			-DANI_SOURCE_CMAKE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
			-DADAPTER_CMAKE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/ani_poco/CMakeLists.txt
			-DADAPTER_POLICY_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/ani_poco/module_policy.c
			-DPOCO_CORE_CMAKE_FILE=${CMAKE_SOURCE_DIR}/poco/CMakeLists.txt
			-DPOCO_CORE_SOURCE_DIR=${CMAKE_SOURCE_DIR}/poco/src
			-P ${CMAKE_CURRENT_SOURCE_DIR}/ani_poco/verify-adapter-target.cmake
	)
	set_tests_properties(ani_poco_adapter_target PROPERTIES TIMEOUT 10)

	add_test(
		NAME ani_poco_module_tooling
		COMMAND ${CMAKE_COMMAND}
			-DPOCO_POE_HELPER=${CMAKE_SOURCE_DIR}/poco/cmake/PocoPoeLibrary.cmake
			-DANI_POE_HELPER=${CMAKE_CURRENT_SOURCE_DIR}/ani_poco/AniPocoPoeLibrary.cmake
			-DPOEKIT_CMAKE_FILE=${CMAKE_SOURCE_DIR}/poco/poekit/CMakeLists.txt
			-P ${CMAKE_CURRENT_SOURCE_DIR}/ani_poco/verify-module-tooling.cmake
	)
	set_tests_properties(ani_poco_module_tooling PROPERTIES TIMEOUT 10)
endif() ## WITH_POCO

set(PJMAIN_SRC_FILES
	a3d.c
	a3ddat.c
	alt.c
	arctan.c
	auto.c
	autoseg.c
	blue.c
	broadcas.c
	browse.c
	brush.c
	celmenu.c
	celpaste.c
	celpull.c
	celtrans.c
	cfit.c
	cleanup.c
	cluster.c
	composit.c
	composmu.c
	csort.c
	cthread.c
	cursor.c
	cutcurs.c
	ellipse.c
	files.c
	findfree.c
	fli.c
	flicel.c
	flicelio.c
	flipath.c
	flood.c
	flxovlay.c
	freem.c
	gel.c
	geometry.c
	globals.c
	grid.c
	hitext.c
	hotkey.c
	inkcashe.c
	inkdot.c
	inkdot2.c
	inkopts.c
	macro.c
	main.c
	mainpull.c
	mask.c
	messages.c
	multimen.c
	muparts.c
	names.c
	onepal.c
	options.c
	overlay.c
	palet2.c
	pencel.c
	penopts.c
	pentools.c
	penwndo.c
	picfiles.c
	pjfont.c
	poly.c
	polyrub.c
	polytool.c
	pstamp.c
	qmacro.c
	qmenu.c
	quickdat.c
	random.c
	rcel.c
	redo.c
	render.c
	resource.c
	savefli.c
	saveseg.c
	selbrush.c
	sep.c
	sizemenu.c
	spiral.c
	spline.c
	stb_impl.c
	about_box.c
	tempflx.c
	tempname.c
	test.c
	textedit.c
	textfile.c
	thikline.c
	tileink.c
	time.c
	timemenu.c
	title.c
	truecolo.c
	tseg.c
	tween.c
	tweendat.c
	tweenhi.c
	tweenlo.c
	tweenmag.c
	vfeelme.c
	vmarqi.c
	vpaint.c
	vpsubs.c
	vsetfnam.c
	vsetting.c
	writeflx.c
	zoom.c
	)

if (WITH_ANI)
	add_executable(
		${PROJECT_NAME}
			${PJMAIN_SRC_FILES}
	)

	## VS-specific stuff
	set_property(DIRECTORY ${PROJECT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})

	set_target_properties(${PROJECT_NAME} PROPERTIES
			VS_DEBUGGER_COMMAND "${CMAKE_INSTALL_PREFIX}/ani.exe"
	)

	set_target_properties(${PROJECT_NAME} PROPERTIES
			VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_INSTALL_PREFIX}"
	)

	# Ensure ani can find libanimhost at runtime
	if(APPLE)
		set_target_properties(${PROJECT_NAME} PROPERTIES
			INSTALL_RPATH "@loader_path"
		)
	elseif(UNIX)
		set_target_properties(${PROJECT_NAME} PROPERTIES
			INSTALL_RPATH "$ORIGIN"
		)
	endif()

# Match util/CLIB_MEMORY so main.c can log pj memory stats on exit
target_compile_definitions(${PROJECT_NAME} PRIVATE CLIB_MEMORY)

target_link_libraries(
		${PROJECT_NAME}
			trdfile
			flilib
			fontlib
			gfxlib
			macinput
			menulib
			picdrive
			pjhigh
			pj_sdl
			raster
			minirex
			softmenu
			trdutil
			wndolib

			SDL3-static
			SDL3_image-static
			miniz
			giflib
#			SDL3_ttf::SDL3_ttf-static
	)

	if (UNIX)
		target_link_libraries(${PROJECT_NAME} m)
	endif()

	if (WITH_POCO)
		target_link_libraries(${PROJECT_NAME} ani_poco_adapter)
		if (APPLE)
			set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY INSTALL_RPATH "@loader_path/lib")
		elseif(UNIX)
			set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY INSTALL_RPATH "$ORIGIN/lib")
		endif()
		# UdQnumber crosses both the libffi C-varargs boundary and the legacy
		# host-to-Poco callback boundary.  Keep this Animator-specific regression
		# outside the standalone Poco project, but consume its implementation
		# through the Ani adapter rather than compiling a second copy.
		add_executable(poco_udqnumber_callback
			"${CMAKE_CURRENT_SOURCE_DIR}/test/udqnumber_callback/udqnumber_callback.c"
		)
		target_compile_definitions(poco_udqnumber_callback PRIVATE
			POCO_UDQNUMBER_CALLBACK_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/udqnumber_callback"
		)
		target_link_libraries(poco_udqnumber_callback PRIVATE ani_poco_adapter)
		add_test(NAME poco_udqnumber_callback COMMAND $<TARGET_FILE:poco_udqnumber_callback>)
		set_tests_properties(poco_udqnumber_callback PROPERTIES TIMEOUT 10)

		# Verify the production runner only reaches Animator bindings through the
		# adapter's public VM registration path.  The adapter is intentionally a
		# static archive of all Animator bindings, so source ownership is checked
		# separately from the real linked-runtime fixture below.
		add_test(
			NAME ani_poco_registration_source
			COMMAND ${CMAKE_COMMAND}
				-DANI_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
				-P ${CMAKE_CURRENT_SOURCE_DIR}/ani_poco/verify-registration.cmake
		)
		set_tests_properties(ani_poco_registration_source PROPERTIES TIMEOUT 10)

		# Link a complete Animator runtime with a renamed application entry point.
		# This lets the fixture use the exact binding objects and host globals that
		# production ani uses, while keeping a small test main() for the VM API.
		set(ANI_POCO_REGISTRATION_RUNTIME_SOURCES ${PJMAIN_SRC_FILES})
		list(REMOVE_ITEM ANI_POCO_REGISTRATION_RUNTIME_SOURCES main.c)
		add_executable(ani_poco_registration
			"${CMAKE_CURRENT_SOURCE_DIR}/test/ani_poco_registration/ani_poco_registration.c"
			"${CMAKE_CURRENT_SOURCE_DIR}/test/ani_poco_registration/animator_runtime_main.c"
			${ANI_POCO_REGISTRATION_RUNTIME_SOURCES}
		)
		target_compile_definitions(ani_poco_registration PRIVATE
			CLIB_MEMORY
			ANI_POCO_REGISTRATION_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/ani_poco_registration"
		)
		target_link_libraries(ani_poco_registration PRIVATE
			trdfile flilib fontlib gfxlib macinput menulib picdrive pjhigh pj_sdl
			raster minirex softmenu trdutil wndolib SDL3-static SDL3_image-static
			miniz giflib ani_poco_adapter
		)
		if (UNIX)
			target_link_libraries(ani_poco_registration PRIVATE m)
		endif()
		add_test(NAME ani_poco_registration COMMAND $<TARGET_FILE:ani_poco_registration>)
		set_tests_properties(ani_poco_registration PROPERTIES TIMEOUT 10)

		# A #pragma poco library for a missing POE module must report a compile
		# error in the Animator runtime, not crash through the windowed error
		# box (errline -> continu_box -> NULL Wscreen).
		add_test(
			NAME ani_poco_missing_module
			COMMAND ${CMAKE_COMMAND}
				-DHARNESS=$<TARGET_FILE:ani_poco_registration>
				-DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/test/ani_poco_registration/missing_module.poc
				-P ${CMAKE_CURRENT_SOURCE_DIR}/test/ani_poco_registration/verify-missing-module.cmake
		)
		set_tests_properties(ani_poco_missing_module PROPERTIES TIMEOUT 30)

		# Runtime coverage: a char** input array must reach a native binding as
		# Popot elements (what po_array_str decodes for Qmenu/Qchoice/Qscroll).
		# The fixture returns non-zero if the string bytes don't survive.
		add_test(
			NAME ani_poco_char_array
			COMMAND $<TARGET_FILE:ani_poco_registration>
				${CMAKE_CURRENT_SOURCE_DIR}/test/ani_poco_registration/char_array_runtime.poc
		)
		set_tests_properties(ani_poco_char_array PROPERTIES TIMEOUT 30)

		# Verify the built Poco-enabled Animator runtime.  Its no-Poco dependency
		# configures a separate Animator build; this verifier checks the installed
		# generic/Ani module locations and the category inventory.
		add_test(
			NAME ani_poco_configurations
			COMMAND ${CMAKE_COMMAND}
				-DBASELINE_MANIFEST=${CMAKE_SOURCE_DIR}/poco/test/binding-baseline.json
				-DBUILD_DIR=${CMAKE_BINARY_DIR}
				-DINSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
				-DANI_EXECUTABLE=$<TARGET_FILE:ani>
				-DANI_REGISTRATION_EXECUTABLE=$<TARGET_FILE:ani_poco_registration>
				-DPOCO_EXECUTABLE=${CMAKE_INSTALL_PREFIX}/poco${CMAKE_EXECUTABLE_SUFFIX}
				-P ${CMAKE_CURRENT_SOURCE_DIR}/ani_poco/verify-configurations.cmake
		)
		set_tests_properties(ani_poco_configurations PROPERTIES
			DEPENDS softmenu_without_poco
			TIMEOUT 30)
	endif()
	
	if (CMAKE_GENERATOR STREQUAL "Xcode")
		set_target_properties(
			${PROJECT_NAME}
			PROPERTIES
				MACOSX_BUNDLE YES
				XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
				XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.vpaint.animator-pro"
		)
	
		if (PLATFORM STREQUAL "OS64")
			set_target_properties(
				${PROJECT_NAME}
				PROPERTIES
					XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2" ## iPhone / iPad
			)
		endif()
	
			## pre-build required or signing will fail
		add_custom_command(
			TARGET ${PROJECT_NAME}
			PRE_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_directory
				"${CMAKE_CURRENT_SOURCE_DIR}/resource" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Resources"
		)
	else()
		INSTALL(TARGETS ${PROJECT_NAME} DESTINATION ./)
		INSTALL(DIRECTORY resource/ DESTINATION ./resource)
		if (MSVC)
			## Make sure to copy over any PDB files so they can be found
			## during debugging sessions.
			INSTALL(FILES $<TARGET_PDB_FILE:${PROJECT_NAME}> DESTINATION bin OPTIONAL)
		endif()
	endif()
	
endif()
