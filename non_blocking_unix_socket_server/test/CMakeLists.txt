file(GLOB SOURCES "*.h" "*.cpp")

add_executable(${COMPONENT}_test ${SOURCES})
target_link_libraries(${COMPONENT}_test PRIVATE ${COMPONENT} GTest::gtest_main)
target_include_directories(${COMPONENT}_test PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})