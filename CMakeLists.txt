cmake_minimum_required(VERSION 3.22)

project(
	WheelerSlowTimeFix
	VERSION 1.3.0
	LANGUAGES CXX
)

option(WSTF_VERBOSE_LOGGING "Enable original detailed runtime logging" OFF)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PLUGIN_AUTHOR "c0kadam")
set(PLUGIN_VERSION_LABEL "1.3.0")
set(PLUGIN_VERSION_TWEAK 0)
set(PLUGIN_PRODUCT_NAME "Wheeler Slow Time Fix")
set(PLUGIN_FILE_DESCRIPTION "Wheeler / Skyrim Souls / For Honor slow-time compatibility fix")
set(PLUGIN_COPYRIGHT "Copyright (c) ${PLUGIN_AUTHOR}")

if("${PROJECT_SOURCE_DIR}" STREQUAL "${PROJECT_BINARY_DIR}")
	message(FATAL_ERROR "in-source builds are not allowed")
endif()

file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS
	"${PROJECT_SOURCE_DIR}/src/*.cpp"
	"${PROJECT_SOURCE_DIR}/src/*.h")

set(GENERATED_INCLUDE_DIR "${PROJECT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED_INCLUDE_DIR}")

configure_file(
	"${PROJECT_SOURCE_DIR}/src/PluginMetadata.h.in"
	"${GENERATED_INCLUDE_DIR}/PluginMetadata.h"
	@ONLY
)

configure_file(
	"${PROJECT_SOURCE_DIR}/src/version.rc.in"
	"${PROJECT_BINARY_DIR}/version.rc"
	@ONLY
)

add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES} "${PROJECT_BINARY_DIR}/version.rc")

target_compile_definitions(${PROJECT_NAME} PRIVATE
	WSTF_VERBOSE_LOGGING=$<BOOL:${WSTF_VERBOSE_LOGGING}>)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)

if(MSVC)
	target_compile_options(
		${PROJECT_NAME}
		PRIVATE
			/permissive-
			/Zc:preprocessor
			/utf-8
			/EHsc
			/sdl
			"$<$<CONFIG:RELEASE>:/Zc:inline;/JMC-;/Ob3;/Zi>"
	)

	target_link_options(
		${PROJECT_NAME}
		PRIVATE
			"$<$<CONFIG:DEBUG>:/INCREMENTAL;/OPT:NOREF;/OPT:NOICF>"
			"$<$<CONFIG:RELEASE>:/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/DEBUG:FULL;/PDBALTPATH:WheelerSlowTimeFix.pdb>"
	)

	set_target_properties(
		${PROJECT_NAME}
		PROPERTIES
			COMPILE_PDB_NAME "${PROJECT_NAME}"
			COMPILE_PDB_OUTPUT_DIRECTORY_RELEASE "${PROJECT_BINARY_DIR}/Release"
			COMPILE_PDB_OUTPUT_DIRECTORY_RELWITHDEBINFO "${PROJECT_BINARY_DIR}/RelWithDebInfo"
			PDB_NAME "${PROJECT_NAME}"
			PDB_OUTPUT_DIRECTORY_RELEASE "${PROJECT_BINARY_DIR}/Release"
			PDB_OUTPUT_DIRECTORY_RELWITHDEBINFO "${PROJECT_BINARY_DIR}/RelWithDebInfo"
	)
endif()

target_include_directories(
	${PROJECT_NAME}
	PRIVATE
		"${GENERATED_INCLUDE_DIR}"
		"${PROJECT_SOURCE_DIR}/src"
)

set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("$ENV{CommonLibSSEPath_NG}" CommonLibSSE EXCLUDE_FROM_ALL)

target_link_libraries(
	${PROJECT_NAME}
	PRIVATE
		CommonLibSSE::CommonLibSSE
)

target_precompile_headers(
	${PROJECT_NAME}
	PRIVATE
		"${PROJECT_SOURCE_DIR}/src/PCH.h"
)
