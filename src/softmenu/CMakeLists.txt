set(SOFTMENU_SRC_FILES
	dstrgets.c
	dstring.c
	keyequiv.c
	loadpull.c
	loadstri.c
	namestri.c
	pullfmt.c
	serrtext.c
	snamestr.c
	sofpanel.c
	softmenu.c
	softok.c
	softmenu_tokenizer.c
	)

add_library(softmenu ${SOFTMENU_SRC_FILES})

add_executable(softmenu_tokenizer_test
	softmenu_tokenizer_test.c
	softmenu_tokenizer.c
	)
target_include_directories(softmenu_tokenizer_test PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/../inc"
	)
add_test(NAME softmenu_tokenizer COMMAND softmenu_tokenizer_test)

# This gate configures and builds the whole Animator executable a second time
# with Poco disabled, so it costs a full from-scratch app build (~45s).  Keep it
# opt-in rather than part of the default CTest run.
option(ANI_ENABLE_WITHOUT_POCO_TEST
	"Enable the slow softmenu_without_poco end-to-end no-Poco build gate"
	OFF
	)

if(ANI_ENABLE_WITHOUT_POCO_TEST)
	add_test(
		NAME softmenu_without_poco
		COMMAND ${CMAKE_COMMAND}
			-DSOURCE_DIR=${CMAKE_SOURCE_DIR}
			-DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/without-poco
			-DTEST_GENERATOR=${CMAKE_GENERATOR}
			-P ${CMAKE_CURRENT_SOURCE_DIR}/verify-without-poco.cmake
		)
endif()
