project(unittests LANGUAGES CXX)


#######  async_cache

add_executable(async_cache
    async_cache.cpp
    )

set_property(TARGET async_cache PROPERTY CXX_STANDARD 20)

add_dependencies( async_cache
    glad
    ${DEPENDS_GTEST}
    )

target_include_directories(async_cache
    PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}
    $<BUILD_INTERFACE:${NSBLAST_ROOT}/include
    $<BUILD_INTERFACE:${NSBLAST_ROOT}/src/lib
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<BUILD_INTERFACE: ${CMAKE_BINARY_DIR}/generated-include
    )

target_link_libraries(async_cache
    ${GTEST_LIBRARIES}
    glad
    ${Boost_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
)

add_test(NAME async_cache COMMAND  async_cache)


#######  perftest

add_executable(perftest
    perftest.cpp
    )

set_property(TARGET perftest PROPERTY CXX_STANDARD 20)

add_dependencies( perftest
    glad
    ${DEPENDS_GTEST}
    )

target_include_directories(perftest
    PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}
    $<BUILD_INTERFACE:${NSBLAST_ROOT}/include
    $<BUILD_INTERFACE:${NSBLAST_ROOT}/src/lib
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<BUILD_INTERFACE: ${CMAKE_BINARY_DIR}/generated-include
    )

target_link_libraries(perftest
    ${GTEST_LIBRARIES}
    glad
    ${Boost_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
)

add_test(NAME perftest COMMAND  perftest)

