project(unittests LANGUAGES CXX)


#######  smart_cache

add_executable(smart_cache
    smart_cache.cpp
    )

set_property(TARGET smart_cache PROPERTY CXX_STANDARD 20)

add_dependencies( smart_cache
    asio-building-bricks
    ${DEPENDS_GTEST}
    )

target_include_directories(smart_cache
    PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}
    $<BUILD_INTERFACE:${NSBLAST_ROOT}/include
    $<BUILD_INTERFACE:${NSBLAST_ROOT}/src/lib
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    )

target_link_libraries(smart_cache
    ${GTEST_LIBRARIES}
    asio-building-bricks
    ${Boost_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
)

add_test(NAME smart_cache COMMAND  smart_cache)

