cmake_minimum_required(VERSION 3.20)

project(imagecpp_package_consumer LANGUAGES C CXX)

enable_testing()

find_package(imagecpp 0.1 REQUIRED CONFIG)

add_executable(imagecpp_package_consumer main.c)
target_compile_features(imagecpp_package_consumer PRIVATE c_std_11)
target_link_libraries(imagecpp_package_consumer PRIVATE imagecpp::imagecpp)

add_test(NAME imagecpp.package-consumer COMMAND imagecpp_package_consumer)
