project(unittests LANGUAGES CXX)


#######  cxx20-simple

add_executable(cxx20-simple
    cxx20-simple.cpp
    )

set_property(TARGET cxx20-simple PROPERTY CXX_STANDARD 20)

add_dependencies(cxx20-simple
    asio-building-bricks
    )

target_include_directories(cxx20-simple
    PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}
    $<BUILD_INTERFACE:${NSBLAST_ROOT}/include
    $<BUILD_INTERFACE:${NSBLAST_ROOT}/src/lib
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
    $<BUILD_INTERFACE: ${CMAKE_BINARY_DIR}/generated-include
    )

target_link_libraries(cxx20-simple
    asio-building-bricks
    ${Boost_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
)

add_test(NAME cxx20-simple COMMAND  cxx20-simple)

