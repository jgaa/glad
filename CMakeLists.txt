cmake_minimum_required(VERSION 3.24)

if (NOT DEFINED ABB_VERSION)
    set(ABB_VERSION 0.0.1)
endif()
set(ABB_VERSION_STR \"${ABB_VERSION}\")

project(asio-building-bricks
    DESCRIPTION "A header-only library with some useful algorithms for asio"
    HOMEPAGE_URL https://github.com/jgaa/asio-building-bricks
    VERSION ${ABB_VERSION}
    LANGUAGES CXX)
set(ABB_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

option(ABB_WITH_TESTS "Enable Tests" ON)
option(ABB_WITH_EXAMPLES "Enable Examples" ON)
option(ABB_USE_STD_UNORDERED_MAP "Use std::unordered_map for the cache" OFF)
option(ABB_USE_BOOST_FLAT_UNORDERED_MAP "Use boost::unordered_flat_map for the cache" OFF)

if (ABB_USE_STD_UNORDERED_MAP AND ABB_USE_BOOST_FLAT_UNORDERED_MAP)
    message(FATAL_ERROR "You can use ABB_USE_STD_UNORDERED_MAP or ABB_USE_BOOST_FLAT_UNORDERED_MAP")
endif()

if (ABB_USE_STD_UNORDERED_MAP)
    set(JGAA_ABB_MAP_TYPE std::unordered_map)
endif()

if (ABB_USE_BOOST_FLAT_UNORDERED_MAP)
    set(JGAA_ABB_MAP_TYPE boost::unordered_flat_map)
endif()

if (ABB_WITH_TESTS)
    find_package(GTest REQUIRED)
endif()


if(NOT DEFINED USE_BOOST_VERSION)
    set(USE_BOOST_VERSION 1.81)
endif()

find_package(Boost ${USE_BOOST_VERSION} REQUIRED COMPONENTS
    system
    program_options
    date_time
    coroutine
    context
    chrono
    )


include(GNUInstallDirs)

add_library(${PROJECT_NAME} INTERFACE
    #include/abb/AsyncCache.hpp
)

target_include_directories( ${PROJECT_NAME} INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(${PROJECT_NAME} INTERFACE cxx_std_20)
CONFIGURE_FILE(${PROJECT_SOURCE_DIR}/cmake/config.h.template ${CMAKE_BINARY_DIR}/generated-include/${PROJECT_NAME}/config.h)


if (ABB_WITH_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if (ABB_WITH_EXAMPLES)
    add_subdirectory(examples)
endif()

install(TARGETS ${PROJECT_NAME}
        EXPORT ${PROJECT_NAME}_Targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

include(CMakePackageConfigHelpers)
write_basic_package_version_file("${PROJECT_NAME}ConfigVersion.cmake"
                                 VERSION ${PROJECT_VERSION}
                                 COMPATIBILITY SameMajorVersion)

configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/cmake/${PROJECT_NAME}Config.cmake.in"
    "${PROJECT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
    INSTALL_DESTINATION
    ${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}/cmake)

install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/${PROJECT_NAME} DESTINATION include)
install(DIRECTORY ${CMAKE_BINARY_DIR}/generated-include/${PROJECT_NAME}/ DESTINATION include)
