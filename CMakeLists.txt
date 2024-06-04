cmake_minimum_required(VERSION 3.15)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(cesium-native
    VERSION 0.1.0
    LANGUAGES CXX C
)

if(NOT DEFINED CMAKE_C_COMPILER_LAUNCHER AND NOT DEFINED CMAKE_CXX_COMPILER_LAUNCHER)
    find_program(CCACHE_FOUND ccache)
    find_program(SCCACHE_FOUND sccache)
    if (SCCACHE_FOUND)
        message("setting SCCACHE to ${SCCACHE_FOUND}")
        set(CMAKE_C_COMPILER_LAUNCHER ${SCCACHE_FOUND})
        set(CMAKE_CXX_COMPILER_LAUNCHER ${SCCACHE_FOUND})
    elseif(CCACHE_FOUND)
        message("setting CCACHE to ${CCACHE_FOUND}")
        set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_FOUND})
        set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_FOUND})
    endif()
endif()

# ccache/sccache only works with /Z7, not /Zi, so tweak the Debug and RelWithDebInfo build flags in the presence of a compiler cache
if(MSVC AND (DEFINED CMAKE_C_COMPILER_LAUNCHER))
    message(DEBUG "Setting MSVC flags to /Z7 for ccache compatibility.  Current flags: ${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/Zi" "/Z7" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/Zi" "/Z7" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
    string(REPLACE "/Zi" "/Z7" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
    string(REPLACE "/Zi" "/Z7" CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")
    message(DEBUG "New flags: ${CMAKE_CXX_FLAGS_DEBUG}")
endif()

option(PRIVATE_CESIUM_SQLITE "ON to rename SQLite symbols to cesium_sqlite3_* so they won't conflict with other SQLite implemenentations" OFF)
option(CESIUM_TRACING_ENABLED "Whether to enable the Cesium performance tracing framework (CESIUM_TRACE_* macros)." OFF)
option(CESIUM_COVERAGE_ENABLED "Whether to enable code coverage" OFF)
option(CESIUM_TESTS_ENABLED "Whether to enable tests" ON)
option(CESIUM_GLM_STRICT_ENABLED "Whether to force strict GLM compile definitions." ON)
option(CESIUM_SPDLOG_HEADER_ONLY "Whether to use the header-only version of spdlog." OFF)

if (CESIUM_TRACING_ENABLED)
    add_compile_definitions(CESIUM_TRACING_ENABLED=1)
endif()

if (CESIUM_SPDLOG_HEADER_ONLY)
  add_compile_definitions(NOGDI)
endif()

# Add Modules
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_LIST_DIR}/extern/cmake-modules/")

if (CESIUM_COVERAGE_ENABLED AND NOT MSVC)
    include(CodeCoverage)
    append_coverage_compiler_flags()
    setup_target_for_coverage_gcovr_html(
        NAME cesium-native-tests-coverage
        EXECUTABLE ctest -j ${PROCESSOR_COUNT}
        EXCLUDE "${PROJECT_SOURCE_DIR}/extern/*" "${PROJECT_BINARY_DIR}"
        DEPENDENCIES cesium-native-tests
    )
endif()

if (NOT DEFINED GLOB_USE_CONFIGURE_DEPENDS)
    set(GLOB_USE_CONFIGURE_DEPENDS OFF CACHE BOOL
        "Controls if cesium-native targets should use configure_depends or not for globbing their sources"
    )
endif()

set(CESIUM_DEBUG_POSTFIX "d" CACHE STRING "debug postfix for cesium native")
set(CESIUM_RELEASE_POSTFIX "" CACHE STRING "release postfix for cesium native")

set(CMAKE_DEBUG_POSTFIX ${CESIUM_DEBUG_POSTFIX})
set(CMAKE_RELEASE_POSTFIX ${CESIUM_RELEASE_POSTFIX})
set(CMAKE_MINSIZEREL_POSTFIX ${CESIUM_RELEASE_POSTFIX})
set(CMAKE_RELWITHDEBINFO_POSTFIX ${CESIUM_RELEASE_POSTFIX})

# Use configure_depends to automatically reconfigure on filesystem
# changes at the expense of computational overhead for CMake to
# determine if new files have been added (-DGLOB_USE_CONFIGURE_DEPENDS).

function(cesium_glob_files out_var_name regexes)
    set(files "")
    foreach(arg ${ARGV})
        list(APPEND regexes_only "${arg}")
    endforeach()
    list(POP_FRONT regexes_only)
    if (GLOB_USE_CONFIGURE_DEPENDS)
        file(GLOB_RECURSE files CONFIGURE_DEPENDS ${regexes_only})
    else()
        file(GLOB files ${regexes_only})
    endif()
    set(${ARGV0} "${files}" PARENT_SCOPE)
endfunction()

# Alter a target to mark its interface include directories as `SYSTEM`.

function(make_interface_directories_system target)
  get_target_property(interface_directories ${target} INTERFACE_INCLUDE_DIRECTORIES)
  set_target_properties(${target}
    PROPERTIES
    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${interface_directories}")
endfunction()

# Shared object support is currently NOT working on Windows
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)

function(configure_cesium_library targetName)
    if (MSVC)
        target_compile_options(${targetName} PRIVATE /W4 /WX /wd4201 /bigobj)
    else()
        target_compile_options(${targetName} PRIVATE -Werror -Wall -Wextra -Wconversion -Wpedantic -Wshadow -Wsign-conversion)
    endif()

    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 13)
        # Disable dangling-reference warning due to amount of false positives: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=109642
        target_compile_options(${targetName} PRIVATE -Wno-dangling-reference)
    endif()

    set_target_properties(${targetName} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
    )

    if (CESIUM_GLM_STRICT_ENABLED)
        target_compile_definitions(
            ${targetName}
            PUBLIC
                GLM_FORCE_XYZW_ONLY # Disable .rgba and .stpq to make it easier to view values from debugger
                GLM_FORCE_EXPLICIT_CTOR # Disallow implicit conversions between dvec3 <-> dvec4, dvec3 <-> fvec3, etc
                GLM_FORCE_SIZE_T_LENGTH # Make vec.length() and vec[idx] use size_t instead of int
        )
    endif()

    if (BUILD_SHARED_LIBS)
        target_compile_definitions(
            ${targetName}
            PUBLIC
                CESIUM_SHARED=${BUILD_SHARED_LIBS}
        )
    endif()

    if (NOT ${targetName} MATCHES "cesium-native-tests")
        string(TOUPPER ${targetName} capitalizedTargetName)
        target_compile_definitions(
            ${targetName}
            PRIVATE
                ${capitalizedTargetName}_BUILDING
        )
        install(TARGETS ${targetName} EXPORT CesiumExports)
    endif()

    if (CESIUM_EXTRA_INCLUDES)
      target_include_directories(${targetName} PRIVATE ${CESIUM_EXTRA_INCLUDES})
    endif()

endfunction()


# Private Libraries
add_subdirectory(extern)

# These libraries override the debug postfix, so re-override it.
set_target_properties(spdlog PROPERTIES DEBUG_POSTFIX "${CESIUM_DEBUG_POSTFIX}")
set_target_properties(tinyxml2 PROPERTIES DEBUG_POSTFIX "${CESIUM_DEBUG_POSTFIX}")

if(CESIUM_SPDLOG_HEADER_ONLY)
  install(TARGETS spdlog_header_only EXPORT CesiumExtern)
  set(CESIUM_SPDLOG_TARGET spdlog::spdlog_header_only)
else()
  install(TARGETS spdlog EXPORT CesiumExtern)
  set(CESIUM_SPDLOG_TARGET spdlog)
endif()

# Public Targets
add_subdirectory(CesiumUtility)
add_subdirectory(CesiumGltf)
add_subdirectory(CesiumGeometry)
add_subdirectory(CesiumGeospatial)
add_subdirectory(CesiumJsonReader)
add_subdirectory(CesiumJsonWriter)
add_subdirectory(CesiumGltfContent)
add_subdirectory(CesiumGltfReader)
add_subdirectory(CesiumGltfWriter)
add_subdirectory(CesiumAsync)
add_subdirectory(Cesium3DTiles)
add_subdirectory(Cesium3DTilesReader)
add_subdirectory(Cesium3DTilesWriter)
add_subdirectory(Cesium3DTilesContent)
add_subdirectory(CesiumRasterOverlays)
add_subdirectory(Cesium3DTilesSelection)
add_subdirectory(CesiumIonClient)
add_subdirectory(CesiumQuantizedMeshTerrain)

# Private Targets
if (CESIUM_TESTS_ENABLED)
    # enable_testing() MUST be called before add_subdirectory or no tests
    # will be found by ctest
    enable_testing()
    add_subdirectory(CesiumNativeTests)
endif()

add_subdirectory(doc)

# Installation of third-party libraries required to use cesium-native
install(TARGETS uriparser EXPORT CesiumExtern)

install(DIRECTORY extern/glm/glm
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    COMPONENT GLM
)
install(TARGETS glm EXPORT CesiumExtern)

install(TARGETS tinyxml2 EXPORT CesiumExtern)

install(TARGETS Async++)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/extern/asyncplusplus/include/async++.h TYPE INCLUDE)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/extern/asyncplusplus/include/async++ TYPE INCLUDE)

install(TARGETS sqlite3 EXPORT CesiumExtern)

install(TARGETS modp_b64 EXPORT CesiumExtern)

install(TARGETS httplib)

# Don't install CSPRNG when building for Universal Windows Platform
if (NOT ${CMAKE_SYSTEM_NAME} MATCHES "WindowsStore")
  install(TARGETS csprng EXPORT CesiumExtern)
endif()

install(TARGETS GSL)
if(NOT ${GSL_INSTALL})
  install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/extern/GSL/include/gsl TYPE INCLUDE)
endif()

# Install with framework destination specified (needed for iOS)
install(TARGETS ktx
		FRAMEWORK
			DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(TARGETS webpdecoder)

install(DIRECTORY $<TARGET_LINKER_FILE:turbojpeg> TYPE LIB)

install(TARGETS zlibstatic EXPORT CesiumExtern)

install(DIRECTORY ${CESIUM_NATIVE_RAPIDJSON_INCLUDE_DIR}/rapidjson TYPE INCLUDE)

install(TARGETS s2geometry EXPORT CesiumExtern)

install(TARGETS expected-lite)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/extern/expected-lite/include/nonstd TYPE INCLUDE)

install(TARGETS meshoptimizer)

install(TARGETS ${CESIUM_NATIVE_DRACO_LIBRARY} EXPORT CesiumExtern)

install(EXPORT CesiumExports
  FILE cesium-nativeTargets.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cesium-native)

install(EXPORT CesiumExtern
  FILE cesium-native-externTargets.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cesium-native)

install(FILES cesium-nativeConfig.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cesium-native)
