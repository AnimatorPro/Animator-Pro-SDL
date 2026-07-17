if(NOT EXISTS "${ADAPTER_CMAKE_FILE}")
	message(FATAL_ERROR
		"Ani Poco adapter target is missing: ${ADAPTER_CMAKE_FILE}")
endif()

if(NOT EXISTS "${ADAPTER_POLICY_SOURCE}")
	message(FATAL_ERROR
		"Ani Poco adapter policy source is missing: ${ADAPTER_POLICY_SOURCE}")
endif()

file(READ "${ADAPTER_CMAKE_FILE}" adapter_cmake)
file(READ "${ANI_SOURCE_CMAKE_FILE}" ani_cmake)
file(READ "${ADAPTER_POLICY_SOURCE}" adapter_policy)
file(READ "${POCO_CORE_CMAKE_FILE}" poco_core_cmake)

if(NOT adapter_cmake MATCHES "add_library[ \t\r\n]*\\([ \t\r\n]*ani_poco_adapter")
	message(FATAL_ERROR "ani_poco_adapter must be an explicit CMake target")
endif()

foreach(required_dependency Poco::poco animhost)
	string(FIND "${adapter_cmake}" "${required_dependency}" dependency_index)
	if(dependency_index EQUAL -1)
		message(FATAL_ERROR
			"ani_poco_adapter must link ${required_dependency}")
	endif()
endforeach()

foreach(binding_source
	pocoa3d.c pocoalt.c pocoblit.c pococel.c pococolo.c pocodos.c
	pocodraw.c pocofile.c pocoflic.c pocofunc.c pocogvar.c pocolibs.c
	pocomode.c pocoqnum.c pocorex.c pocotext.c pocotime.c pocotur.c
	pocotwee.c pocouser.c pocopicdrive.c packcmap.c qpoco.c qpocoed.c)
	string(FIND "${adapter_cmake}" "${binding_source}" adapter_source_index)
	if(adapter_source_index EQUAL -1)
		message(FATAL_ERROR
			"ani_poco_adapter must own ${binding_source}")
	endif()
	string(FIND "${ani_cmake}" "${binding_source}" ani_source_index)
	if(NOT ani_source_index EQUAL -1)
		message(FATAL_ERROR
			"${binding_source} must not remain in the ani executable source list")
	endif()
endforeach()

if(NOT adapter_policy MATCHES "#include <poco/poco.h>" OR
	NOT adapter_policy MATCHES "#include \"jimk.h\"")
	message(FATAL_ERROR
		"the adapter policy must be the explicit boundary between Poco and Animator headers")
endif()

if(NOT adapter_policy MATCHES "animhost_ensure_pocolib" OR
	NOT adapter_policy MATCHES "allow_legacy_poe")
	message(FATAL_ERROR
		"the adapter policy must opt legacy POE into animhost_ensure_pocolib")
endif()

if(poco_core_cmake MATCHES "animhost" OR poco_core_cmake MATCHES "src/inc")
	message(FATAL_ERROR
		"Poco core must not acquire Animator build dependencies")
endif()

file(GLOB_RECURSE poco_core_sources
	"${POCO_CORE_SOURCE_DIR}/*.c"
	"${POCO_CORE_SOURCE_DIR}/*.h")
foreach(poco_core_source IN LISTS poco_core_sources)
	file(READ "${poco_core_source}" poco_core_source_text)
	if(poco_core_source_text MATCHES "#include[ \t]*\"jimk.h\"" OR
		poco_core_source_text MATCHES "animhost_ensure_pocolib" OR
		poco_core_source_text MATCHES "#include[ \t]*\"resource.h\"")
		message(FATAL_ERROR
			"Poco core must not import Animator-private state: ${poco_core_source}")
	endif()
endforeach()
