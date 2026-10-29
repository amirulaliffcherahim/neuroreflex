cmake_minimum_required(VERSION 3.21)
project(neuroreflex-host-tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

if("$ENV{UNA_SDK}" STREQUAL "")
    message(FATAL_ERROR "UNA_SDK environment variable is not set.")
endif()

# Same pinned GoogleTest the SDK's own host tests use.
include($ENV{UNA_SDK}/Tests/Host/cmake/FetchGoogleTest.cmake)

set(LIBS "${CMAKE_CURRENT_LIST_DIR}/../../Software/Libs")

add_executable(neuroreflex-host-tests
    PvtEngine_test.cpp
    Presenter_test.cpp
    Presenter_contrast_test.cpp
    Presenter_design_test.cpp
    "${LIBS}/Sources/PvtEngine.cpp"
    "${LIBS}/Sources/Presenter.cpp"
    "${LIBS}/Sources/Readiness.cpp"
    Readiness_test.cpp
)
target_include_directories(neuroreflex-host-tests PRIVATE "${LIBS}/Header" "$ENV{UNA_SDK}/Libs/Header")
target_link_libraries(neuroreflex-host-tests PRIVATE GTest::gtest_main)

enable_testing()
add_test(NAME neuroreflex-host-tests COMMAND neuroreflex-host-tests)


