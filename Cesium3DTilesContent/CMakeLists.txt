add_library(Cesium3DTilesContent "")
configure_cesium_library(Cesium3DTilesContent)

cesium_glob_files(CESIUM_3DTILES_CONTENT_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/*.cpp
    ${CMAKE_CURRENT_LIST_DIR}/generated/src/*.cpp
)
cesium_glob_files(CESIUM_3DTILES_CONTENT_HEADERS
    ${CMAKE_CURRENT_LIST_DIR}/src/*.h
    ${CMAKE_CURRENT_LIST_DIR}/generated/src/*.h
)
cesium_glob_files(CESIUM_3DTILES_CONTENT_PUBLIC_HEADERS
    ${CMAKE_CURRENT_LIST_DIR}/include/Cesium3DTilesContent/*.h
    ${CMAKE_CURRENT_LIST_DIR}/generated/include/Cesium3DTilesContent/*.h
)
cesium_glob_files(CESIUM_3DTILES_CONTENT_TEST_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/test/*.cpp
)
cesium_glob_files(CESIUM_3DTILES_CONTENT_TEST_HEADERS
    ${CMAKE_CURRENT_LIST_DIR}/test/*.h
)

set_target_properties(Cesium3DTilesContent
    PROPERTIES
        TEST_SOURCES "${CESIUM_3DTILES_CONTENT_TEST_SOURCES}"
        TEST_HEADERS "${CESIUM_3DTILES_CONTENT_TEST_HEADERS}"
        TEST_DATA_DIR ${CMAKE_CURRENT_LIST_DIR}/test/data
)

set_target_properties(Cesium3DTilesContent
    PROPERTIES
    PUBLIC_HEADER "${CESIUM_3DTILES_CONTENT_PUBLIC_HEADERS}"
)

target_sources(
    Cesium3DTilesContent
    PRIVATE
        ${CESIUM_3DTILES_CONTENT_SOURCES}
        ${CESIUM_3DTILES_CONTENT_HEADERS}
        ${CESIUM_3DTILES_CONTENT_PUBLIC_HEADERS}
)

target_include_directories(
    Cesium3DTilesContent
    SYSTEM PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/generated/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
        ${CMAKE_CURRENT_LIST_DIR}/generated/src
        # Necessary for `draco/draco_features.h`
        ${CESIUM_NATIVE_DRACO_FEATURES_DIR}
        ${CESIUM_NATIVE_DRACO_INCLUDE_DIR}
)

target_link_libraries(Cesium3DTilesContent
    PUBLIC
        Cesium3DTiles
        Cesium3DTilesReader
        CesiumAsync
        CesiumGeometry
        CesiumGeospatial
        CesiumGltf
        CesiumGltfContent
        CesiumGltfReader
        CesiumUtility
    PRIVATE
        libmorton
)

install(TARGETS Cesium3DTilesContent
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Cesium3DTilesContent
)
