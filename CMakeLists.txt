cmake_minimum_required(VERSION 3.20)

project(
    imagecpp
    VERSION 0.1.0
    DESCRIPTION "Native C/C++ runtime for local image intelligence"
    LANGUAGES C CXX
)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

option(IMAGECPP_BUILD_CLI "Build the imagecpp command-line tool" ON)
option(IMAGECPP_BUILD_TESTS "Build image.cpp tests" OFF)
option(IMAGECPP_BUILD_SHARED "Build libimagecpp as a shared library" OFF)

function(imagecpp_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

set(IMAGECPP_LIBRARY_TYPE STATIC)
if(IMAGECPP_BUILD_SHARED)
    set(IMAGECPP_LIBRARY_TYPE SHARED)
endif()

add_library(imagecpp ${IMAGECPP_LIBRARY_TYPE}
    src/core/status.cpp
    src/image/image.cpp
    src/image/layout.cpp
    src/image/resize.cpp
    src/runtime/runtime.cpp
    src/version.cpp
)
add_library(imagecpp::imagecpp ALIAS imagecpp)

target_compile_features(imagecpp PRIVATE cxx_std_17)
target_include_directories(imagecpp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_definitions(imagecpp
    PRIVATE IMAGECPP_BUILDING_LIBRARY
    PUBLIC $<$<BOOL:${IMAGECPP_BUILD_SHARED}>:IMAGECPP_SHARED>
)
set_target_properties(imagecpp PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

imagecpp_enable_warnings(imagecpp)

if(IMAGECPP_BUILD_CLI)
    add_executable(imagecpp_cli
        app/main.cpp
        app/pnm.cpp
    )
    target_compile_features(imagecpp_cli PRIVATE cxx_std_17)
    target_link_libraries(imagecpp_cli PRIVATE imagecpp::imagecpp)
    imagecpp_enable_warnings(imagecpp_cli)
    set_target_properties(imagecpp_cli PROPERTIES OUTPUT_NAME imagecpp)
endif()

if(IMAGECPP_BUILD_TESTS)
    enable_testing()
    add_executable(imagecpp_tests tests/test_imagecpp.cpp)
    target_compile_features(imagecpp_tests PRIVATE cxx_std_17)
    target_link_libraries(imagecpp_tests PRIVATE imagecpp::imagecpp)
    imagecpp_enable_warnings(imagecpp_tests)
    add_test(NAME imagecpp.unit COMMAND imagecpp_tests)
    add_executable(imagecpp_c_api_test tests/test_c_api.c)
    target_link_libraries(imagecpp_c_api_test PRIVATE imagecpp::imagecpp)
    imagecpp_enable_warnings(imagecpp_c_api_test)
    set_target_properties(imagecpp_c_api_test PROPERTIES LINKER_LANGUAGE CXX)
    add_test(NAME imagecpp.c-api COMMAND imagecpp_c_api_test)
    if(IMAGECPP_BUILD_CLI)
        add_test(NAME imagecpp.cli.inspect COMMAND imagecpp_cli inspect)
    endif()
endif()

install(TARGETS imagecpp
    EXPORT imagecppTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
if(IMAGECPP_BUILD_CLI)
    install(TARGETS imagecpp_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/imagecppConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
configure_package_config_file(
    cmake/imagecppConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/imagecppConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/imagecpp"
)
install(EXPORT imagecppTargets
    FILE imagecppTargets.cmake
    NAMESPACE imagecpp::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/imagecpp"
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/imagecppConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/imagecppConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/imagecpp"
)
