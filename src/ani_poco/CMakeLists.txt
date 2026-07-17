cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

# This is the explicit Animator-to-Poco ownership boundary.  Poco core owns
# the interpreter and generic module ABI; the Animator-only bindings, runner,
# and retained native-POE compatibility policy live here.
set(ANI_POCO_ADAPTER_SOURCES
	"${CMAKE_CURRENT_LIST_DIR}/../pocoa3d.c"
	"${CMAKE_CURRENT_LIST_DIR}/../pocoalt.c"
	"${CMAKE_CURRENT_LIST_DIR}/../pocoblit.c"
	"${CMAKE_CURRENT_LIST_DIR}/../pococel.c"
	"${CMAKE_CURRENT_LIST_DIR}/../pococolo.c"
	"${CMAKE_CURRENT_LIST_DIR}/../pocodos.c"
	"${CMAKE_CURRENT_LIST_DIR}/../pocodraw.c"
	"${CMAKE_CURRENT_LIST_DIR}/../pocofile.c"
	"${CMAKE_CURRENT_LIST_DIR}/../pocoflic.c"
	"${CMAKE_CURRENT_LIST_DIR}/../pocofunc.c"
	"${CMAKE_CURRENT_LIST_DIR}/../pocogvar.c"
	"${CMAKE_CURRENT_LIST_DIR}/../pocolibs.c"
	"${CMAKE_CURRENT_LIST_DIR}/../pocomode.c"
	"${CMAKE_CURRENT_LIST_DIR}/../pocoqnum.c"
	"${CMAKE_CURRENT_LIST_DIR}/../pocorex.c"
	"${CMAKE_CURRENT_LIST_DIR}/../pocotext.c"
	"${CMAKE_CURRENT_LIST_DIR}/../pocotime.c"
	"${CMAKE_CURRENT_LIST_DIR}/../pocotur.c"
	"${CMAKE_CURRENT_LIST_DIR}/../pocotwee.c"
	"${CMAKE_CURRENT_LIST_DIR}/../pocouser.c"
	"${CMAKE_CURRENT_LIST_DIR}/../pocopicdrive.c"
	"${CMAKE_CURRENT_LIST_DIR}/../packcmap.c"
	"${CMAKE_CURRENT_LIST_DIR}/../qpoco.c"
	"${CMAKE_CURRENT_LIST_DIR}/../qpocoed.c"
	"${CMAKE_CURRENT_LIST_DIR}/module_policy.c"
)

add_library(ani_poco_adapter STATIC ${ANI_POCO_ADAPTER_SOURCES})

# The only include path exported by this target is the narrow Animator adapter
# interface.  Its implementation gets Animator-private headers explicitly,
# while Poco's canonical include directory arrives through Poco::poco.
target_include_directories(ani_poco_adapter
	PUBLIC
		$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>
	PRIVATE
		"${CMAKE_CURRENT_LIST_DIR}/../inc"
)

target_link_libraries(ani_poco_adapter
	PUBLIC
		Poco::poco
	PRIVATE
		animhost
)
