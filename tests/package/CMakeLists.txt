cmake_minimum_required(VERSION 3.20)

project(imagecpp_package_consumer LANGUAGES C CXX)

enable_testing()

find_package(imagecpp 0.1 REQUIRED CONFIG)

set(IMAGECPP_PACKAGE_VLM_MODEL "" CACHE FILEPATH "Optional installed-package VLM language model")
set(IMAGECPP_PACKAGE_VLM_PROJECTION_MODEL "" CACHE FILEPATH "Optional installed-package VLM projection model")
set(IMAGECPP_PACKAGE_TEST_IMAGE "" CACHE FILEPATH "Optional installed-package VLM test image")

add_executable(imagecpp_package_consumer main.c)
target_compile_features(imagecpp_package_consumer PRIVATE c_std_11)
target_link_libraries(imagecpp_package_consumer PRIVATE imagecpp::imagecpp)

add_test(NAME imagecpp.package-consumer COMMAND imagecpp_package_consumer)

if(IMAGECPP_PACKAGE_VLM_MODEL AND IMAGECPP_PACKAGE_VLM_PROJECTION_MODEL AND IMAGECPP_PACKAGE_TEST_IMAGE)
    add_executable(imagecpp_package_vlm_consumer ../test_vlm_c_api.c)
    target_compile_features(imagecpp_package_vlm_consumer PRIVATE c_std_11)
    target_compile_definitions(imagecpp_package_vlm_consumer PRIVATE
        "IMAGECPP_TEST_VLM_MODEL_PATH=\"${IMAGECPP_PACKAGE_VLM_MODEL}\""
        "IMAGECPP_TEST_VLM_PROJECTION_MODEL_PATH=\"${IMAGECPP_PACKAGE_VLM_PROJECTION_MODEL}\""
        "IMAGECPP_TEST_IMAGE_PATH=\"${IMAGECPP_PACKAGE_TEST_IMAGE}\""
    )
    target_link_libraries(imagecpp_package_vlm_consumer PRIVATE imagecpp::imagecpp)
    add_test(NAME imagecpp.package-vlm-consumer COMMAND imagecpp_package_vlm_consumer)
    set_tests_properties(imagecpp.package-vlm-consumer PROPERTIES TIMEOUT 120)
endif()
