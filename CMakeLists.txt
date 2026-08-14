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
option(IMAGECPP_WITH_WEBP "Build WebP decoding and encoding support" ON)
option(IMAGECPP_WITH_SAM3 "Build SAM 2/SAM 3/EdgeTAM segmentation support" ON)
option(IMAGECPP_WITH_STABLE_DIFFUSION "Build generation, editing, and ESRGAN support" ON)
set(IMAGECPP_TEST_SAM_MODEL "" CACHE FILEPATH "Optional SAM/EdgeTAM model used by integration tests")
set(IMAGECPP_TEST_DIFFUSION_MODEL "" CACHE FILEPATH "Optional diffusion model used by integration tests")
set(IMAGECPP_TEST_UPSCALER_MODEL "" CACHE FILEPATH "Optional ESRGAN model used by integration tests")

function(imagecpp_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

function(imagecpp_add_model_download target filename url sha256 display_name)
    set(model_output "${CMAKE_CURRENT_SOURCE_DIR}/models/${filename}")
    add_custom_target(${target}
        COMMAND ${CMAKE_COMMAND}
            "-DIMAGECPP_MODEL_OUTPUT=${model_output}"
            "-DIMAGECPP_MODEL_URL=${url}"
            "-DIMAGECPP_MODEL_SHA256=${sha256}"
            "-DIMAGECPP_MODEL_NAME=${display_name}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/download_model.cmake"
        BYPRODUCTS "${model_output}"
        COMMENT "Verifying ${display_name}"
        VERBATIM
    )
endfunction()

set(IMAGECPP_LIBRARY_TYPE STATIC)
if(IMAGECPP_BUILD_SHARED)
    set(IMAGECPP_LIBRARY_TYPE SHARED)
endif()

add_library(imagecpp ${IMAGECPP_LIBRARY_TYPE}
    src/core/status.cpp
    src/image/codec.cpp
    src/image/image.cpp
    src/image/layout.cpp
    src/image/resize.cpp
    src/model/model.cpp
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
        ${CMAKE_CURRENT_SOURCE_DIR}/external/stb
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

if(IMAGECPP_WITH_WEBP)
    set(WEBP_BUILD_ANIM_UTILS OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_CWEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_DWEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_GIF2WEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_IMG2WEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_VWEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_WEBPINFO OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_LIBWEBPMUX OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_WEBPMUX OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_FUZZTEST OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_WEBP_JS OFF CACHE BOOL "" FORCE)
    set(WEBP_LINK_STATIC ON CACHE BOOL "" FORCE)
    set(IMAGECPP_SAVED_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF)
    add_subdirectory(external/libwebp)
    set(BUILD_SHARED_LIBS ${IMAGECPP_SAVED_BUILD_SHARED_LIBS})
    unset(IMAGECPP_SAVED_BUILD_SHARED_LIBS)
    if(NOT TARGET WebP::webp)
        add_library(WebP::webp ALIAS webp)
    endif()
    target_link_libraries(imagecpp PRIVATE WebP::webp)
    target_compile_definitions(imagecpp PRIVATE IMAGECPP_WITH_WEBP=1)
endif()

if(IMAGECPP_WITH_SAM3 OR IMAGECPP_WITH_STABLE_DIFFUSION)
    set(GGML_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GGML_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GGML_BACKEND_DL OFF CACHE BOOL "" FORCE)
    set(GGML_CCACHE OFF CACHE BOOL "" FORCE)
    set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
    set(GGML_OPENMP OFF CACHE BOOL "" FORCE)
    if(APPLE)
        set(GGML_METAL ON CACHE BOOL "" FORCE)
        set(GGML_METAL_EMBED_LIBRARY ON CACHE BOOL "" FORCE)
    endif()

    add_compile_definitions(GGML_MAX_NAME=128)
    set(IMAGECPP_SAVED_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF)
    add_subdirectory(external/ggml)
    set(BUILD_SHARED_LIBS ${IMAGECPP_SAVED_BUILD_SHARED_LIBS})
    unset(IMAGECPP_SAVED_BUILD_SHARED_LIBS)
    foreach(ggml_target ggml ggml-base ${GGML_AVAILABLE_BACKENDS})
        if(TARGET ${ggml_target})
            set_target_properties(${ggml_target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
        endif()
    endforeach()
endif()

if(IMAGECPP_WITH_SAM3)
    add_library(imagecpp_sam3_engine OBJECT external/sam3/sam3.cpp)
    target_compile_features(imagecpp_sam3_engine PRIVATE cxx_std_14)
    target_include_directories(imagecpp_sam3_engine PRIVATE
        external/sam3
        external/sam3/stb
    )
    target_compile_definitions(imagecpp_sam3_engine PRIVATE
        SAM3_LOG_LEVEL=0
        STB_IMAGE_STATIC
        STB_IMAGE_WRITE_STATIC
    )
    target_link_libraries(imagecpp_sam3_engine PRIVATE ggml::ggml)
    set_target_properties(imagecpp_sam3_engine PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        POSITION_INDEPENDENT_CODE YES
    )

    target_sources(imagecpp PRIVATE
        src/provider/sam3_provider.cpp
        $<TARGET_OBJECTS:imagecpp_sam3_engine>
    )
    target_include_directories(imagecpp PRIVATE external/sam3)
    target_link_libraries(imagecpp PRIVATE ggml::ggml)
    target_compile_definitions(imagecpp PRIVATE IMAGECPP_WITH_SAM3=1)

    imagecpp_add_model_download(
        imagecpp_model_edgetam
        edgetam_q4_0.ggml
        "https://huggingface.co/PABannier/sam3.cpp/resolve/main/edgetam_q4_0.ggml"
        a8a35e35fb9a1b6f099c3f35e3024548b0fc979c2a4184642562804192496e09
        "EdgeTAM Q4"
    )
endif()

if(IMAGECPP_WITH_STABLE_DIFFUSION)
    set(SD_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SD_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(SD_BUILD_SHARED_GGML_LIB OFF CACHE BOOL "" FORCE)
    set(SD_USE_SYSTEM_GGML OFF CACHE BOOL "" FORCE)
    if(APPLE)
        set(SD_METAL ON CACHE BOOL "" FORCE)
    endif()
    add_subdirectory(external/stable-diffusion)
    set_target_properties(stable-diffusion PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES ""
        INTERFACE_LINK_LIBRARIES "$<LINK_ONLY:ggml>"
        POSITION_INDEPENDENT_CODE ON
    )
    target_sources(imagecpp PRIVATE src/provider/stable_diffusion_provider.cpp)
    target_include_directories(imagecpp PRIVATE external/stable-diffusion/include)
    target_link_libraries(imagecpp PRIVATE stable-diffusion)
    target_compile_definitions(imagecpp PRIVATE
        IMAGECPP_WITH_STABLE_DIFFUSION=1
        $<$<BOOL:${APPLE}>:IMAGECPP_SD_METAL=1>
    )
    imagecpp_add_model_download(
        imagecpp_model_sd15_q4
        v1-5-pruned_Q4_0.gguf
        "https://huggingface.co/kostakoff/stable-diffusion-v1-5-GGUF/resolve/main/v1-5-pruned_Q4_0.gguf"
        24bcd54c1d1f0354a1cd19d07ad9a20771d43fde8318d505d71bcd84b078f20a
        "Stable Diffusion 1.5 Q4_0"
    )
    imagecpp_add_model_download(
        imagecpp_model_realesrgan
        RealESRGAN_x4plus_anime_6B.pth
        "https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.2.4/RealESRGAN_x4plus_anime_6B.pth"
        f872d837d3c90ed2e05227bed711af5671a6fd1c9f7d7e91c911a61f155e99da
        "RealESRGAN x4plus anime 6B"
    )
endif()

if(IMAGECPP_BUILD_CLI)
    add_executable(imagecpp_cli
        app/main.cpp
        app/model_commands.cpp
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
    target_compile_definitions(imagecpp_tests PRIVATE
        $<$<BOOL:${IMAGECPP_WITH_WEBP}>:IMAGECPP_TEST_WITH_WEBP=1>
        $<$<BOOL:${IMAGECPP_WITH_SAM3}>:IMAGECPP_TEST_WITH_SAM3=1>
        $<$<BOOL:${IMAGECPP_WITH_STABLE_DIFFUSION}>:IMAGECPP_TEST_WITH_STABLE_DIFFUSION=1>
    )
    imagecpp_enable_warnings(imagecpp_tests)
    add_test(NAME imagecpp.unit COMMAND imagecpp_tests)
    add_executable(imagecpp_c_api_test tests/test_c_api.c)
    target_link_libraries(imagecpp_c_api_test PRIVATE imagecpp::imagecpp)
    imagecpp_enable_warnings(imagecpp_c_api_test)
    set_target_properties(imagecpp_c_api_test PROPERTIES LINKER_LANGUAGE CXX)
    add_test(NAME imagecpp.c-api COMMAND imagecpp_c_api_test)
    if(IMAGECPP_BUILD_CLI)
        add_test(NAME imagecpp.cli.inspect COMMAND imagecpp_cli inspect)
        add_test(
            NAME imagecpp.cli.resize
            COMMAND imagecpp_cli resize
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/2x2.ppm"
                "${CMAKE_CURRENT_BINARY_DIR}/imagecpp-cli-resize.png"
                5x3
                bilinear
        )
        if(IMAGECPP_WITH_SAM3 AND IMAGECPP_TEST_SAM_MODEL)
            add_test(
                NAME imagecpp.cli.segment-model
                COMMAND imagecpp_cli segment
                    "${IMAGECPP_TEST_SAM_MODEL}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/2x2.ppm"
                    "${CMAKE_CURRENT_BINARY_DIR}/imagecpp-cli-mask.png"
                    --point 1,1
            )
        endif()
        if(IMAGECPP_WITH_STABLE_DIFFUSION AND IMAGECPP_TEST_DIFFUSION_MODEL)
            add_test(
                NAME imagecpp.cli.generate-model
                COMMAND imagecpp_cli generate
                    "${IMAGECPP_TEST_DIFFUSION_MODEL}"
                    "${CMAKE_CURRENT_BINARY_DIR}/imagecpp-cli-generated.png"
                    "a red square on a white background"
                    --size 64x64
                    --steps 1
                    --seed 1
            )
            set_tests_properties(imagecpp.cli.generate-model PROPERTIES TIMEOUT 180)
        endif()
        if(IMAGECPP_WITH_STABLE_DIFFUSION AND IMAGECPP_TEST_UPSCALER_MODEL)
            add_test(
                NAME imagecpp.cli.upscale-model
                COMMAND imagecpp_cli upscale
                    "${IMAGECPP_TEST_UPSCALER_MODEL}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/2x2.ppm"
                    "${CMAKE_CURRENT_BINARY_DIR}/imagecpp-cli-upscaled.png"
                    --factor 4
            )
            set_tests_properties(imagecpp.cli.upscale-model PROPERTIES TIMEOUT 60)
        endif()
    endif()
endif()

install(TARGETS imagecpp
    EXPORT imagecppTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
if(IMAGECPP_WITH_SAM3 OR IMAGECPP_WITH_STABLE_DIFFUSION)
    set(IMAGECPP_GGML_TARGETS ggml ggml-base)
    foreach(backend IN LISTS GGML_AVAILABLE_BACKENDS)
        if(TARGET ${backend})
            list(APPEND IMAGECPP_GGML_TARGETS ${backend})
        endif()
    endforeach()
    get_target_property(IMAGECPP_GGML_PUBLIC_HEADERS ggml PUBLIC_HEADER)
    set_target_properties(ggml PROPERTIES PUBLIC_HEADER "")
    install(TARGETS ${IMAGECPP_GGML_TARGETS}
        EXPORT imagecppTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    set_target_properties(ggml PROPERTIES PUBLIC_HEADER "${IMAGECPP_GGML_PUBLIC_HEADERS}")
    unset(IMAGECPP_GGML_PUBLIC_HEADERS)
endif()
if(IMAGECPP_WITH_STABLE_DIFFUSION)
    get_target_property(IMAGECPP_SD_PUBLIC_HEADERS stable-diffusion PUBLIC_HEADER)
    set_target_properties(stable-diffusion PROPERTIES PUBLIC_HEADER "")
    install(TARGETS stable-diffusion
        EXPORT imagecppTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    set_target_properties(stable-diffusion PROPERTIES PUBLIC_HEADER "${IMAGECPP_SD_PUBLIC_HEADERS}")
    unset(IMAGECPP_SD_PUBLIC_HEADERS)
endif()
if(IMAGECPP_BUILD_CLI)
    install(TARGETS imagecpp_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES LICENSE THIRD_PARTY.md DESTINATION ${CMAKE_INSTALL_DATADIR}/imagecpp)
install(FILES external/stb/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/imagecpp/licenses RENAME stb.txt)
install(FILES external/libwebp/COPYING DESTINATION ${CMAKE_INSTALL_DATADIR}/imagecpp/licenses RENAME libwebp.txt)
if(IMAGECPP_WITH_SAM3)
    install(FILES external/sam3/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/imagecpp/licenses RENAME sam3.cpp.txt)
endif()
if(IMAGECPP_WITH_SAM3 OR IMAGECPP_WITH_STABLE_DIFFUSION)
    install(FILES external/ggml/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/imagecpp/licenses RENAME ggml.txt)
endif()
if(IMAGECPP_WITH_STABLE_DIFFUSION)
    install(FILES external/stable-diffusion/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/imagecpp/licenses RENAME stable-diffusion.cpp.txt)
endif()

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
