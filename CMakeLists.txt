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
option(IMAGECPP_BUILD_HTTP_SERVER "Build the native HTTP API into the imagecpp command-line tool" ON)
option(IMAGECPP_BUILD_TESTS "Build image.cpp tests" OFF)
option(IMAGECPP_BUILD_SHARED "Build libimagecpp as a shared library" OFF)
option(IMAGECPP_WITH_WEBP "Build WebP decoding and encoding support" ON)
option(IMAGECPP_WITH_SAM3 "Build SAM 2/SAM 3/EdgeTAM segmentation support" ON)
option(IMAGECPP_WITH_STABLE_DIFFUSION "Build generation, editing, and ESRGAN support" ON)
option(IMAGECPP_WITH_DEPTH_ANYTHING "Build Depth Anything 2/3 support" ON)
option(IMAGECPP_WITH_CLIP "Build CLIP embedding and zero-shot classification support" ON)
option(IMAGECPP_WITH_TESSERACT "Build Tesseract OCR and document layout support" ON)
option(IMAGECPP_WITH_VLM "Build image captioning and visual question answering support" ON)
set(IMAGECPP_TEST_SAM_MODEL "" CACHE FILEPATH "Optional SAM/EdgeTAM model used by integration tests")
set(IMAGECPP_TEST_SAM3_MODEL "" CACHE FILEPATH "Optional full SAM 3 model used by detection integration tests")
set(IMAGECPP_TEST_DIFFUSION_MODEL "" CACHE FILEPATH "Optional diffusion model used by integration tests")
set(IMAGECPP_TEST_UPSCALER_MODEL "" CACHE FILEPATH "Optional ESRGAN model used by integration tests")
set(IMAGECPP_TEST_DEPTH_MODEL "" CACHE FILEPATH "Optional Depth Anything model used by integration tests")
set(IMAGECPP_TEST_CLIP_MODEL "" CACHE FILEPATH "Optional two-tower CLIP GGUF model used by integration tests")
set(IMAGECPP_TEST_OCR_MODEL "" CACHE FILEPATH "Optional Tesseract traineddata model used by integration tests")
set(IMAGECPP_TEST_VLM_MODEL "" CACHE FILEPATH "Optional VLM language GGUF used by integration tests")
set(IMAGECPP_TEST_VLM_PROJECTION_MODEL "" CACHE FILEPATH "Optional VLM projection GGUF used by integration tests")

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
    src/workflow/cutout.cpp
    src/workflow/cutout_internal.cpp
    src/workflow/grounded_cutout.cpp
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

if(IMAGECPP_WITH_TESSERACT)
    set(IMAGECPP_SAVED_SKIP_INSTALL_RULES ${CMAKE_SKIP_INSTALL_RULES})
    set(CMAKE_SKIP_INSTALL_RULES ON)
    set(BUILD_PROG OFF CACHE BOOL "" FORCE)
    set(SW_BUILD OFF CACHE BOOL "" FORCE)
    set(SYM_LINK OFF CACHE BOOL "" FORCE)
    set(STRICT_CONF OFF CACHE BOOL "" FORCE)
    set(ENABLE_ZLIB OFF CACHE BOOL "" FORCE)
    set(ENABLE_PNG OFF CACHE BOOL "" FORCE)
    set(ENABLE_GIF OFF CACHE BOOL "" FORCE)
    set(ENABLE_JPEG OFF CACHE BOOL "" FORCE)
    set(ENABLE_TIFF OFF CACHE BOOL "" FORCE)
    set(ENABLE_WEBP OFF CACHE BOOL "" FORCE)
    set(ENABLE_OPENJPEG OFF CACHE BOOL "" FORCE)
    set(IMAGECPP_SAVED_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF)
    add_subdirectory(external/leptonica EXCLUDE_FROM_ALL)
    target_sources(leptonica PRIVATE src/provider/leptonica_no_subprocess.c)
    target_compile_definitions(leptonica PRIVATE system=imagecpp_leptonica_system_disabled)

    set(IMAGECPP_LEPTONICA_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/leptonica")
    set(IMAGECPP_LEPTONICA_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/external/leptonica")
    set(IMAGECPP_LEPTONICA_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}/imagecpp-leptonica-config")
    file(MAKE_DIRECTORY "${IMAGECPP_LEPTONICA_CONFIG_DIR}")
    configure_file(cmake/LeptonicaConfig.cmake.in
        "${IMAGECPP_LEPTONICA_CONFIG_DIR}/LeptonicaConfig.cmake" @ONLY)
    write_basic_package_version_file(
        "${IMAGECPP_LEPTONICA_CONFIG_DIR}/LeptonicaConfigVersion.cmake"
        VERSION 1.87.0
        COMPATIBILITY SameMajorVersion
    )
    set(Leptonica_DIR "${IMAGECPP_LEPTONICA_CONFIG_DIR}" CACHE PATH "" FORCE)

    set(OPENMP_BUILD OFF CACHE BOOL "" FORCE)
    set(GRAPHICS_DISABLED ON CACHE BOOL "" FORCE)
    set(DISABLED_LEGACY_ENGINE OFF CACHE BOOL "" FORCE)
    set(ENABLE_LTO OFF CACHE BOOL "" FORCE)
    set(ENABLE_NATIVE OFF CACHE BOOL "" FORCE)
    set(BUILD_TRAINING_TOOLS OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(DISABLE_TIFF ON CACHE BOOL "" FORCE)
    set(DISABLE_ARCHIVE ON CACHE BOOL "" FORCE)
    set(DISABLE_CURL ON CACHE BOOL "" FORCE)
    set(INSTALL_CONFIGS OFF CACHE BOOL "" FORCE)
    set(ENABLE_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
    set(ENABLE_CCACHE OFF CACHE BOOL "" FORCE)
    set(IMAGECPP_SAVED_CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH})
    list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/tesseract")
    add_subdirectory(external/tesseract EXCLUDE_FROM_ALL)
    set(CMAKE_MODULE_PATH ${IMAGECPP_SAVED_CMAKE_MODULE_PATH})
    unset(IMAGECPP_SAVED_CMAKE_MODULE_PATH)
    set(CMAKE_SKIP_INSTALL_RULES ${IMAGECPP_SAVED_SKIP_INSTALL_RULES})
    unset(IMAGECPP_SAVED_SKIP_INSTALL_RULES)
    set(BUILD_SHARED_LIBS ${IMAGECPP_SAVED_BUILD_SHARED_LIBS})
    unset(IMAGECPP_SAVED_BUILD_SHARED_LIBS)

    set_target_properties(leptonica libtesseract PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
    )
    target_include_directories(libtesseract BEFORE PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/external/tesseract"
    )
    target_compile_definitions(libtesseract PRIVATE TESSERACT_DISABLE_DEBUG_FONTS=1)
    target_compile_definitions(leptonica PRIVATE MINIMUM_SEVERITY=5)
    set_target_properties(tesseract PROPERTIES EXCLUDE_FROM_ALL YES)
    target_sources(imagecpp PRIVATE src/provider/tesseract_provider.cpp)
    target_include_directories(imagecpp PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/external/tesseract/include"
    )
    target_link_libraries(imagecpp PRIVATE libtesseract)
    target_compile_definitions(imagecpp PRIVATE IMAGECPP_WITH_TESSERACT=1)

    imagecpp_add_model_download(
        imagecpp_model_tesseract_eng
        eng.traineddata
        "https://raw.githubusercontent.com/tesseract-ocr/tessdata_fast/87416418657359cb625c412a48b6e1d6d41c29bd/eng.traineddata"
        7d4322bd2a7749724879683fc3912cb542f19906c83bcc1a52132556427170b2
        "Tesseract fast English"
    )
endif()

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

if(IMAGECPP_WITH_SAM3 OR IMAGECPP_WITH_STABLE_DIFFUSION OR IMAGECPP_WITH_DEPTH_ANYTHING OR IMAGECPP_WITH_CLIP OR
   IMAGECPP_WITH_VLM)
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
    if(IMAGECPP_WITH_VLM)
        add_subdirectory(external/llama/ggml)
    else()
        add_subdirectory(external/ggml)
    endif()
    if(UNIX)
        target_sources(ggml-base PRIVATE src/provider/ggml_no_subprocess.c)
        target_compile_definitions(ggml-base PRIVATE
            fork=imagecpp_ggml_fork_disabled
            execlp=imagecpp_ggml_execlp_disabled
        )
    endif()
    set(BUILD_SHARED_LIBS ${IMAGECPP_SAVED_BUILD_SHARED_LIBS})
    unset(IMAGECPP_SAVED_BUILD_SHARED_LIBS)
    foreach(ggml_target ggml ggml-base ${GGML_AVAILABLE_BACKENDS})
        if(TARGET ${ggml_target})
            set_target_properties(${ggml_target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
        endif()
    endforeach()
endif()

if(IMAGECPP_WITH_VLM)
    set(LLAMA_BUILD_COMMON OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_APP OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_MTMD ON CACHE BOOL "" FORCE)
    set(LLAMA_OPENSSL OFF CACHE BOOL "" FORCE)
    set(LLAMA_SUBPROCESS OFF CACHE BOOL "" FORCE)
    set(MTMD_VIDEO OFF CACHE BOOL "" FORCE)
    set(IMAGECPP_SAVED_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF)
    add_subdirectory(external/llama)
    set(BUILD_SHARED_LIBS ${IMAGECPP_SAVED_BUILD_SHARED_LIBS})
    unset(IMAGECPP_SAVED_BUILD_SHARED_LIBS)
    set_target_properties(llama mtmd PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
    )
    set_target_properties(llama PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/llama/include>"
    )
    set_target_properties(mtmd PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES ""
    )
    target_compile_definitions(mtmd PRIVATE STB_IMAGE_STATIC STB_IMAGE_WRITE_STATIC)
    target_sources(imagecpp PRIVATE src/provider/vlm_provider.cpp)
    target_include_directories(imagecpp PRIVATE
        external/llama/include
        external/llama/tools/mtmd
    )
    target_link_libraries(imagecpp PRIVATE mtmd llama ggml::ggml)
    target_compile_definitions(imagecpp PRIVATE IMAGECPP_WITH_VLM=1)

    imagecpp_add_model_download(
        imagecpp_model_smolvlm_q8
        SmolVLM-256M-Instruct-Q8_0.gguf
        "https://huggingface.co/ggml-org/SmolVLM-256M-Instruct-GGUF/resolve/b9e4379657e1450d04d02eec8e345667265b0a00/SmolVLM-256M-Instruct-Q8_0.gguf"
        2a31195d3769c0b0fd0a4906201666108834848db768af11de1d2cef7cd35e65
        "SmolVLM 256M Instruct Q8_0"
    )
    imagecpp_add_model_download(
        imagecpp_model_smolvlm_mmproj_q8
        mmproj-SmolVLM-256M-Instruct-Q8_0.gguf
        "https://huggingface.co/ggml-org/SmolVLM-256M-Instruct-GGUF/resolve/b9e4379657e1450d04d02eec8e345667265b0a00/mmproj-SmolVLM-256M-Instruct-Q8_0.gguf"
        7e943f7c53f0382a6fc41b6ee0c2def63ba4fded9ab8ed039cc9e2ab905e0edd
        "SmolVLM 256M vision projection Q8_0"
    )
endif()

if(IMAGECPP_WITH_CLIP)
    add_library(imagecpp_clip_engine STATIC external/clipcpp/clip.cpp)
    target_compile_features(imagecpp_clip_engine PRIVATE cxx_std_17)
    target_include_directories(imagecpp_clip_engine PRIVATE
        external/clipcpp
        external/stb
    )
    target_compile_definitions(imagecpp_clip_engine PRIVATE STB_IMAGE_STATIC)
    target_link_libraries(imagecpp_clip_engine PRIVATE ggml::ggml)
    set_target_properties(imagecpp_clip_engine PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        POSITION_INDEPENDENT_CODE YES
    )

    target_sources(imagecpp PRIVATE src/provider/clip_provider.cpp)
    target_include_directories(imagecpp BEFORE PRIVATE external/clipcpp)
    target_link_libraries(imagecpp PRIVATE imagecpp_clip_engine ggml::ggml)
    target_compile_definitions(imagecpp PRIVATE IMAGECPP_WITH_CLIP=1)

    imagecpp_add_model_download(
        imagecpp_model_clip
        clip-vit-b-32-laion2b-q4_0.gguf
        "https://huggingface.co/mys/ggml_CLIP-ViT-B-32-laion2B-s34B-b79K/resolve/main/CLIP-ViT-B-32-laion2B-s34B-b79K_ggml-model-q4_0.gguf"
        66aa926f26c468a5eb400e97b8bbcf80444f6f0fc59d6927f8bea47548a04ce2
        "LAION CLIP ViT-B/32 Q4_0"
    )
endif()

if(IMAGECPP_WITH_DEPTH_ANYTHING)
    add_library(imagecpp_depth_engine STATIC
        external/depth-anything/src/attention.cpp
        external/depth-anything/src/backend.cpp
        external/depth-anything/src/cam_pose.cpp
        external/depth-anything/src/dino_backbone.cpp
        external/depth-anything/src/dpt_blocks.cpp
        external/depth-anything/src/dpt_head.cpp
        external/depth-anything/src/engine.cpp
        external/depth-anything/src/gs_adapter.cpp
        external/depth-anything/src/gs_head.cpp
        external/depth-anything/src/image_io.cpp
        external/depth-anything/src/linalg.cpp
        external/depth-anything/src/model_loader.cpp
        external/depth-anything/src/nested.cpp
        external/depth-anything/src/preprocess.cpp
        external/depth-anything/src/ray_pose.cpp
        external/depth-anything/src/rope2d.cpp
        external/depth-anything/src/uv_posembed.cpp
        external/depth-anything/src/vit_block.cpp
        external/depth-anything/src/winograd.cpp
    )
    target_compile_features(imagecpp_depth_engine PRIVATE cxx_std_17)
    target_include_directories(imagecpp_depth_engine PRIVATE
        external/depth-anything/include
        external/depth-anything/src
        external/depth-anything/third_party/stb
    )
    target_compile_definitions(imagecpp_depth_engine PRIVATE STB_IMAGE_STATIC)
    target_link_libraries(imagecpp_depth_engine PRIVATE ggml::ggml)
    set_target_properties(imagecpp_depth_engine PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        POSITION_INDEPENDENT_CODE YES
    )

    target_sources(imagecpp PRIVATE src/provider/depth_anything_provider.cpp)
    target_include_directories(imagecpp PRIVATE external/depth-anything/src)
    target_link_libraries(imagecpp PRIVATE imagecpp_depth_engine ggml::ggml)
    target_compile_definitions(imagecpp PRIVATE IMAGECPP_WITH_DEPTH_ANYTHING=1)

    imagecpp_add_model_download(
        imagecpp_model_depth_anything
        depth-anything-base-q4_k.gguf
        "https://huggingface.co/mudler/depth-anything.cpp-gguf/resolve/main/depth-anything-base-q4_k.gguf"
        43cd45d00f9024f4319f4beabd73155db5132e4b575bc52eff4131262c9d78f1
        "Depth Anything 3 Base Q4_K"
    )
endif()

if(IMAGECPP_WITH_SAM3)
    add_library(imagecpp_sam3_engine OBJECT
        external/sam3/sam3.cpp
        src/provider/sam3_no_subprocess.c
    )
    target_compile_features(imagecpp_sam3_engine PRIVATE cxx_std_14)
    target_include_directories(imagecpp_sam3_engine PRIVATE
        external/sam3
        external/sam3/stb
    )
    target_compile_definitions(imagecpp_sam3_engine PRIVATE
        SAM3_LOG_LEVEL=0
        popen=imagecpp_sam3_disabled_popen
        pclose=imagecpp_sam3_disabled_pclose
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
    imagecpp_add_model_download(
        imagecpp_model_sam3_q4
        sam3-q4_0.ggml
        "https://huggingface.co/PABannier/sam3.cpp/resolve/main/sam3-q4_0.ggml"
        5dafc790c8493319f542f575e718084f4e0a452fd7e483c64853d33ffe3f1889
        "SAM 3 Q4_0"
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

add_custom_target(imagecpp_models_starter)
foreach(IMAGECPP_STARTER_MODEL_TARGET
        imagecpp_model_edgetam
        imagecpp_model_depth_anything
        imagecpp_model_realesrgan
        imagecpp_model_clip
        imagecpp_model_tesseract_eng
        imagecpp_model_smolvlm_q8
        imagecpp_model_smolvlm_mmproj_q8)
    if(TARGET ${IMAGECPP_STARTER_MODEL_TARGET})
        add_dependencies(imagecpp_models_starter ${IMAGECPP_STARTER_MODEL_TARGET})
    endif()
endforeach()

add_custom_target(imagecpp_models_full)
add_dependencies(imagecpp_models_full imagecpp_models_starter)
if(TARGET imagecpp_model_sd15_q4)
    add_dependencies(imagecpp_models_full imagecpp_model_sd15_q4)
endif()
if(TARGET imagecpp_model_sam3_q4)
    add_dependencies(imagecpp_models_full imagecpp_model_sam3_q4)
endif()

if(IMAGECPP_BUILD_HTTP_SERVER)
    find_package(Threads REQUIRED)
    add_library(imagecpp_httplib STATIC
        external/llama/vendor/cpp-httplib/httplib.cpp
    )
    target_compile_features(imagecpp_httplib PRIVATE cxx_std_17)
    target_include_directories(imagecpp_httplib PUBLIC external/llama/vendor/cpp-httplib)
    target_link_libraries(imagecpp_httplib PRIVATE Threads::Threads)
    set_target_properties(imagecpp_httplib PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(MSVC)
        target_compile_options(imagecpp_httplib PRIVATE /w)
    else()
        target_compile_options(imagecpp_httplib PRIVATE -w)
    endif()
    if(WIN32)
        target_link_libraries(imagecpp_httplib PUBLIC ws2_32)
    endif()

    add_library(imagecpp_http_server STATIC src/server/http_server.cpp)
    target_compile_features(imagecpp_http_server PRIVATE cxx_std_17)
    target_include_directories(imagecpp_http_server
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
        PRIVATE external/llama/vendor/nlohmann
    )
    target_link_libraries(imagecpp_http_server
        PUBLIC imagecpp::imagecpp
        PRIVATE imagecpp_httplib Threads::Threads
    )
    set_target_properties(imagecpp_http_server PROPERTIES POSITION_INDEPENDENT_CODE ON)
    imagecpp_enable_warnings(imagecpp_http_server)
endif()

if(IMAGECPP_BUILD_CLI)
    add_executable(imagecpp_cli
        app/depth_command.cpp
        app/detect_command.cpp
        app/main.cpp
        app/model_commands.cpp
        app/ocr_command.cpp
        app/vlm_command.cpp
    )
    target_compile_features(imagecpp_cli PRIVATE cxx_std_17)
    target_link_libraries(imagecpp_cli PRIVATE imagecpp::imagecpp)
    imagecpp_enable_warnings(imagecpp_cli)
    set_target_properties(imagecpp_cli PROPERTIES OUTPUT_NAME imagecpp)
    if(IMAGECPP_BUILD_HTTP_SERVER)
        target_link_libraries(imagecpp_cli PRIVATE imagecpp_http_server)
        target_compile_definitions(imagecpp_cli PRIVATE IMAGECPP_BUILD_HTTP_SERVER=1)
    endif()
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
        $<$<BOOL:${IMAGECPP_WITH_DEPTH_ANYTHING}>:IMAGECPP_TEST_WITH_DEPTH_ANYTHING=1>
        $<$<BOOL:${IMAGECPP_WITH_CLIP}>:IMAGECPP_TEST_WITH_CLIP=1>
        $<$<BOOL:${IMAGECPP_WITH_TESSERACT}>:IMAGECPP_TEST_WITH_TESSERACT=1>
        $<$<BOOL:${IMAGECPP_WITH_VLM}>:IMAGECPP_TEST_WITH_VLM=1>
    )
    imagecpp_enable_warnings(imagecpp_tests)
    add_test(NAME imagecpp.unit COMMAND imagecpp_tests)
    add_executable(imagecpp_c_api_test tests/test_c_api.c)
    target_link_libraries(imagecpp_c_api_test PRIVATE imagecpp::imagecpp)
    imagecpp_enable_warnings(imagecpp_c_api_test)
    set_target_properties(imagecpp_c_api_test PROPERTIES LINKER_LANGUAGE CXX)
    add_test(NAME imagecpp.c-api COMMAND imagecpp_c_api_test)
    if(IMAGECPP_BUILD_HTTP_SERVER)
        add_executable(imagecpp_http_server_test tests/test_http_server.cpp)
        target_compile_features(imagecpp_http_server_test PRIVATE cxx_std_17)
        target_link_libraries(imagecpp_http_server_test PRIVATE imagecpp_http_server imagecpp_httplib)
        imagecpp_enable_warnings(imagecpp_http_server_test)
        add_test(NAME imagecpp.http-server COMMAND imagecpp_http_server_test)
        set_tests_properties(imagecpp.http-server PROPERTIES TIMEOUT 30)
        if(IMAGECPP_WITH_VLM AND IMAGECPP_TEST_VLM_MODEL AND IMAGECPP_TEST_VLM_PROJECTION_MODEL)
            add_test(
                NAME imagecpp.http-server.vlm-model
                COMMAND imagecpp_http_server_test
                    "${IMAGECPP_TEST_VLM_MODEL}"
                    "${IMAGECPP_TEST_VLM_PROJECTION_MODEL}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/external/stable-diffusion/assets/a lovely cat.png"
            )
            set_tests_properties(imagecpp.http-server.vlm-model PROPERTIES TIMEOUT 120)
        endif()
    endif()
    if(IMAGECPP_WITH_TESSERACT AND IMAGECPP_TEST_OCR_MODEL)
        add_executable(imagecpp_ocr_c_api_test tests/test_ocr_c_api.c)
        target_link_libraries(imagecpp_ocr_c_api_test PRIVATE imagecpp::imagecpp)
        imagecpp_enable_warnings(imagecpp_ocr_c_api_test)
        set_target_properties(imagecpp_ocr_c_api_test PROPERTIES LINKER_LANGUAGE CXX)
        add_test(
            NAME imagecpp.c-api.ocr-model
            COMMAND imagecpp_ocr_c_api_test
                "${IMAGECPP_TEST_OCR_MODEL}"
                "${CMAKE_CURRENT_BINARY_DIR}/imagecpp-ocr-fixture.png"
        )
        set_tests_properties(imagecpp.c-api.ocr-model PROPERTIES TIMEOUT 30)
    endif()
    if(IMAGECPP_WITH_VLM AND IMAGECPP_TEST_VLM_MODEL AND IMAGECPP_TEST_VLM_PROJECTION_MODEL)
        add_executable(imagecpp_vlm_c_api_test tests/test_vlm_c_api.c)
        target_link_libraries(imagecpp_vlm_c_api_test PRIVATE imagecpp::imagecpp)
        target_compile_definitions(imagecpp_vlm_c_api_test PRIVATE
            "IMAGECPP_TEST_VLM_MODEL_PATH=\"${IMAGECPP_TEST_VLM_MODEL}\""
            "IMAGECPP_TEST_VLM_PROJECTION_MODEL_PATH=\"${IMAGECPP_TEST_VLM_PROJECTION_MODEL}\""
            "IMAGECPP_TEST_IMAGE_PATH=\"${CMAKE_CURRENT_SOURCE_DIR}/external/stable-diffusion/assets/a lovely cat.png\""
        )
        imagecpp_enable_warnings(imagecpp_vlm_c_api_test)
        set_target_properties(imagecpp_vlm_c_api_test PROPERTIES LINKER_LANGUAGE CXX)
        add_test(NAME imagecpp.c-api.vlm-model COMMAND imagecpp_vlm_c_api_test)
        set_tests_properties(imagecpp.c-api.vlm-model PROPERTIES TIMEOUT 120)
    endif()
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
        if(IMAGECPP_WITH_TESSERACT AND IMAGECPP_TEST_OCR_MODEL)
            add_test(
                NAME imagecpp.cli.ocr-model
                COMMAND imagecpp_cli ocr
                    "${IMAGECPP_TEST_OCR_MODEL}"
                    "${CMAKE_CURRENT_BINARY_DIR}/imagecpp-ocr-fixture.png"
                    --psm line
            )
            set_tests_properties(imagecpp.cli.ocr-model PROPERTIES
                DEPENDS imagecpp.c-api.ocr-model
                PASS_REGULAR_EXPRESSION "HELL"
                TIMEOUT 30
            )
            add_test(
                NAME imagecpp.cli.ocr-json-model
                COMMAND imagecpp_cli ocr
                    "${IMAGECPP_TEST_OCR_MODEL}"
                    "${CMAKE_CURRENT_BINARY_DIR}/imagecpp-ocr-fixture.png"
                    --psm line
                    --json
            )
            set_tests_properties(imagecpp.cli.ocr-json-model PROPERTIES
                DEPENDS imagecpp.c-api.ocr-model
                PASS_REGULAR_EXPRESSION "\"regions\":\\["
                TIMEOUT 30
            )
        endif()
        if(IMAGECPP_WITH_VLM AND IMAGECPP_TEST_VLM_MODEL AND IMAGECPP_TEST_VLM_PROJECTION_MODEL)
            add_test(
                NAME imagecpp.cli.caption-model
                COMMAND imagecpp_cli caption
                    "${IMAGECPP_TEST_VLM_MODEL}"
                    "${IMAGECPP_TEST_VLM_PROJECTION_MODEL}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/external/stable-diffusion/assets/a lovely cat.png"
                    --max-tokens 16
                    --temperature 0
                    --cpu
                    --json
            )
            set_tests_properties(imagecpp.cli.caption-model PROPERTIES
                PASS_REGULAR_EXPRESSION "\"text\":\".+\""
                TIMEOUT 120
            )
            add_test(
                NAME imagecpp.cli.ask-model
                COMMAND imagecpp_cli ask
                    "${IMAGECPP_TEST_VLM_MODEL}"
                    "${IMAGECPP_TEST_VLM_PROJECTION_MODEL}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/external/stable-diffusion/assets/a lovely cat.png"
                    "What animal is in the image? Answer with one word."
                    --max-tokens 8
                    --temperature 0
                    --cpu
            )
            set_tests_properties(imagecpp.cli.ask-model PROPERTIES
                PASS_REGULAR_EXPRESSION "[Cc]at"
                TIMEOUT 120
            )
            add_test(
                NAME imagecpp.cli.ask-stream-model
                COMMAND imagecpp_cli ask
                    "${IMAGECPP_TEST_VLM_MODEL}"
                    "${IMAGECPP_TEST_VLM_PROJECTION_MODEL}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/external/stable-diffusion/assets/a lovely cat.png"
                    "What animal is in the image? Answer with one word."
                    --max-tokens 8
                    --temperature 0
                    --cpu
                    --stream
            )
            set_tests_properties(imagecpp.cli.ask-stream-model PROPERTIES
                PASS_REGULAR_EXPRESSION "[Cc]at"
                TIMEOUT 120
            )
        endif()
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
        if(IMAGECPP_WITH_SAM3 AND IMAGECPP_TEST_SAM3_MODEL)
            add_executable(imagecpp_detect_c_api_test tests/test_detect_c_api.c)
            target_link_libraries(imagecpp_detect_c_api_test PRIVATE imagecpp::imagecpp)
            target_compile_definitions(imagecpp_detect_c_api_test PRIVATE
                "IMAGECPP_TEST_SAM3_MODEL_PATH=\"${IMAGECPP_TEST_SAM3_MODEL}\""
                "IMAGECPP_TEST_IMAGE_PATH=\"${CMAKE_CURRENT_SOURCE_DIR}/external/stable-diffusion/assets/a lovely cat.png\""
            )
            imagecpp_enable_warnings(imagecpp_detect_c_api_test)
            set_target_properties(imagecpp_detect_c_api_test PROPERTIES LINKER_LANGUAGE CXX)
            add_test(NAME imagecpp.c-api.detect-model COMMAND imagecpp_detect_c_api_test)
            set_tests_properties(imagecpp.c-api.detect-model PROPERTIES TIMEOUT 120)
            add_test(
                NAME imagecpp.cli.detect-model
                COMMAND imagecpp_cli detect
                    "${IMAGECPP_TEST_SAM3_MODEL}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/external/stable-diffusion/assets/a lovely cat.png"
                    cat
                    --threshold 0.3
            )
            set_tests_properties(imagecpp.cli.detect-model PROPERTIES TIMEOUT 120)
            add_test(
                NAME imagecpp.cli.ground-model
                COMMAND imagecpp_cli ground
                    "${IMAGECPP_TEST_SAM3_MODEL}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/external/stable-diffusion/assets/a lovely cat.png"
                    "${CMAKE_CURRENT_BINARY_DIR}/imagecpp-cli-ground-mask.png"
                    cat
                    --threshold 0.3
            )
            set_tests_properties(imagecpp.cli.ground-model PROPERTIES TIMEOUT 120)
            add_test(
                NAME imagecpp.cli.extract-model
                COMMAND imagecpp_cli extract
                    "${IMAGECPP_TEST_SAM3_MODEL}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/external/stable-diffusion/assets/a lovely cat.png"
                    "${CMAKE_CURRENT_BINARY_DIR}/imagecpp-cli-extract.png"
                    cat
                    --threshold 0.3
                    --all
                    --padding 8
            )
            set_tests_properties(imagecpp.cli.extract-model PROPERTIES TIMEOUT 120)
            if(IMAGECPP_WITH_STABLE_DIFFUSION AND IMAGECPP_TEST_UPSCALER_MODEL)
                add_test(
                    NAME imagecpp.cli.extract-upscale-model
                    COMMAND imagecpp_cli extract
                        "${IMAGECPP_TEST_SAM3_MODEL}"
                        "${CMAKE_CURRENT_SOURCE_DIR}/external/stable-diffusion/assets/a lovely cat.png"
                        "${CMAKE_CURRENT_BINARY_DIR}/imagecpp-cli-extract-upscaled.png"
                        cat
                        --threshold 0.3
                        --padding 8
                        --upscaler "${IMAGECPP_TEST_UPSCALER_MODEL}"
                        --factor 4
                        --tile 128
                )
                set_tests_properties(imagecpp.cli.extract-upscale-model PROPERTIES TIMEOUT 180)
            endif()
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
        if(IMAGECPP_WITH_SAM3 AND IMAGECPP_WITH_STABLE_DIFFUSION AND IMAGECPP_TEST_SAM_MODEL AND
           IMAGECPP_TEST_UPSCALER_MODEL)
            add_executable(imagecpp_workflow_c_api_test tests/test_workflow_c_api.c)
            target_link_libraries(imagecpp_workflow_c_api_test PRIVATE imagecpp::imagecpp)
            target_compile_definitions(imagecpp_workflow_c_api_test PRIVATE
                "IMAGECPP_TEST_SAM_MODEL_PATH=\"${IMAGECPP_TEST_SAM_MODEL}\""
                "IMAGECPP_TEST_UPSCALER_MODEL_PATH=\"${IMAGECPP_TEST_UPSCALER_MODEL}\""
                "IMAGECPP_TEST_IMAGE_PATH=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/data/2x2.ppm\""
            )
            imagecpp_enable_warnings(imagecpp_workflow_c_api_test)
            set_target_properties(imagecpp_workflow_c_api_test PROPERTIES LINKER_LANGUAGE CXX)
            add_test(NAME imagecpp.c-api.cutout-workflow COMMAND imagecpp_workflow_c_api_test)
            set_tests_properties(imagecpp.c-api.cutout-workflow PROPERTIES TIMEOUT 120)
            add_test(
                NAME imagecpp.cli.cutout-workflow
                COMMAND imagecpp_cli cutout
                    "${IMAGECPP_TEST_SAM_MODEL}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/2x2.ppm"
                    "${CMAKE_CURRENT_BINARY_DIR}/imagecpp-cli-cutout.png"
                    --point 1,1
                    --keep-canvas
                    --upscaler "${IMAGECPP_TEST_UPSCALER_MODEL}"
                    --factor 4
            )
            set_tests_properties(imagecpp.cli.cutout-workflow PROPERTIES TIMEOUT 120)
        endif()
        if(IMAGECPP_WITH_DEPTH_ANYTHING AND IMAGECPP_TEST_DEPTH_MODEL)
            add_test(
                NAME imagecpp.cli.depth-model
                COMMAND imagecpp_cli depth
                    "${IMAGECPP_TEST_DEPTH_MODEL}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/2x2.ppm"
                    "${CMAKE_CURRENT_BINARY_DIR}/imagecpp-cli-depth.png"
            )
            set_tests_properties(imagecpp.cli.depth-model PROPERTIES TIMEOUT 60)
        endif()
        if(IMAGECPP_WITH_CLIP AND IMAGECPP_TEST_CLIP_MODEL)
            add_executable(imagecpp_clip_c_api_test tests/test_clip_c_api.c)
            target_link_libraries(imagecpp_clip_c_api_test PRIVATE imagecpp::imagecpp)
            target_compile_definitions(imagecpp_clip_c_api_test PRIVATE
                "IMAGECPP_TEST_CLIP_MODEL_PATH=\"${IMAGECPP_TEST_CLIP_MODEL}\""
                "IMAGECPP_TEST_IMAGE_PATH=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/data/2x2.ppm\""
            )
            imagecpp_enable_warnings(imagecpp_clip_c_api_test)
            set_target_properties(imagecpp_clip_c_api_test PROPERTIES LINKER_LANGUAGE CXX)
            add_test(NAME imagecpp.c-api.clip-model COMMAND imagecpp_clip_c_api_test)
            set_tests_properties(imagecpp.c-api.clip-model PROPERTIES TIMEOUT 60)
            add_test(
                NAME imagecpp.cli.classify-model
                COMMAND imagecpp_cli classify
                    "${IMAGECPP_TEST_CLIP_MODEL}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/tests/data/2x2.ppm"
                    cat dog car
            )
            set_tests_properties(imagecpp.cli.classify-model PROPERTIES TIMEOUT 60)
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
if(IMAGECPP_WITH_TESSERACT)
    install(TARGETS libtesseract leptonica
        EXPORT imagecppTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()
if(IMAGECPP_WITH_VLM)
    get_target_property(IMAGECPP_LLAMA_PUBLIC_HEADERS llama PUBLIC_HEADER)
    get_target_property(IMAGECPP_MTMD_PUBLIC_HEADERS mtmd PUBLIC_HEADER)
    get_target_property(IMAGECPP_MTMD_PRIVATE_HEADERS mtmd PRIVATE_HEADER)
    set_target_properties(llama mtmd PROPERTIES PUBLIC_HEADER "")
    set_target_properties(mtmd PROPERTIES PRIVATE_HEADER "")
    install(TARGETS llama mtmd
        EXPORT imagecppTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    set_target_properties(llama PROPERTIES PUBLIC_HEADER "${IMAGECPP_LLAMA_PUBLIC_HEADERS}")
    set_target_properties(mtmd PROPERTIES PUBLIC_HEADER "${IMAGECPP_MTMD_PUBLIC_HEADERS}")
    set_target_properties(mtmd PROPERTIES PRIVATE_HEADER "${IMAGECPP_MTMD_PRIVATE_HEADERS}")
    unset(IMAGECPP_LLAMA_PUBLIC_HEADERS)
    unset(IMAGECPP_MTMD_PUBLIC_HEADERS)
    unset(IMAGECPP_MTMD_PRIVATE_HEADERS)
endif()
if(IMAGECPP_WITH_SAM3 OR IMAGECPP_WITH_STABLE_DIFFUSION OR IMAGECPP_WITH_DEPTH_ANYTHING OR IMAGECPP_WITH_CLIP OR
   IMAGECPP_WITH_VLM)
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
if(IMAGECPP_WITH_DEPTH_ANYTHING)
    install(TARGETS imagecpp_depth_engine
        EXPORT imagecppTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()
if(IMAGECPP_WITH_CLIP)
    install(TARGETS imagecpp_clip_engine
        EXPORT imagecppTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()
if(IMAGECPP_BUILD_CLI)
    install(TARGETS imagecpp_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES LICENSE THIRD_PARTY.md DESTINATION ${CMAKE_INSTALL_DATADIR}/imagecpp)
install(FILES external/stb/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/imagecpp/licenses RENAME stb.txt)
install(FILES external/libwebp/COPYING DESTINATION ${CMAKE_INSTALL_DATADIR}/imagecpp/licenses RENAME libwebp.txt)
if(IMAGECPP_BUILD_HTTP_SERVER)
    install(FILES external/llama/vendor/cpp-httplib/LICENSE
        DESTINATION ${CMAKE_INSTALL_DATADIR}/imagecpp/licenses RENAME cpp-httplib.txt)
    install(FILES external/llama/licenses/LICENSE-jsonhpp
        DESTINATION ${CMAKE_INSTALL_DATADIR}/imagecpp/licenses RENAME nlohmann-json.txt)
endif()
if(IMAGECPP_WITH_TESSERACT)
    install(FILES external/tesseract/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/imagecpp/licenses RENAME tesseract.txt)
    install(FILES external/leptonica/leptonica-license.txt DESTINATION ${CMAKE_INSTALL_DATADIR}/imagecpp/licenses RENAME leptonica.txt)
endif()
if(IMAGECPP_WITH_SAM3)
    install(FILES external/sam3/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/imagecpp/licenses RENAME sam3.cpp.txt)
endif()
if(IMAGECPP_WITH_SAM3 OR IMAGECPP_WITH_STABLE_DIFFUSION OR IMAGECPP_WITH_DEPTH_ANYTHING OR IMAGECPP_WITH_CLIP OR
   IMAGECPP_WITH_VLM)
    if(IMAGECPP_WITH_VLM)
        install(FILES external/llama/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/imagecpp/licenses RENAME ggml.txt)
    else()
        install(FILES external/ggml/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/imagecpp/licenses RENAME ggml.txt)
    endif()
endif()
if(IMAGECPP_WITH_VLM)
    install(FILES external/llama/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/imagecpp/licenses RENAME llama.cpp.txt)
endif()
if(IMAGECPP_WITH_STABLE_DIFFUSION)
    install(FILES external/stable-diffusion/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/imagecpp/licenses RENAME stable-diffusion.cpp.txt)
endif()
if(IMAGECPP_WITH_DEPTH_ANYTHING)
    install(FILES external/depth-anything/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/imagecpp/licenses RENAME depth-anything.cpp.txt)
endif()
if(IMAGECPP_WITH_CLIP)
    install(FILES external/clipcpp/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/imagecpp/licenses RENAME clip.cpp.txt)
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
