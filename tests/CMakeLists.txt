find_package(GTest REQUIRED)

add_executable(vkb_tests
    test_vulkan_manager.cpp
    test_ipc_channel.cpp
)

target_include_directories(vkb_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${Vulkan_INCLUDE_DIRS}"
    "${SKIA_INCLUDE_DIRS}"
)

target_link_libraries(vkb_tests PRIVATE
    GTest::gtest_main
    Vulkan::Vulkan
    Qt6::Core
    Qt6::Network
)

include(GoogleTest)
gtest_discover_tests(vkb_tests)
