include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.17.0
  GIT_SHALLOW TRUE
)
# Do not install GoogleTest together with this project and skip GoogleMock.
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

add_executable(tsq_test ts_queue_test.cpp thread_pool_test.cpp)
target_link_libraries(tsq_test PRIVATE tsq GTest::gtest_main)
gtest_discover_tests(tsq_test PROPERTIES TIMEOUT 10)
