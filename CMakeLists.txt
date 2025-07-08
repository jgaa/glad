cmake_minimum_required(VERSION 3.24)

if (NOT DEFINED GLAD_VERSION)
    set(GLAD_VERSION 0.2.0)
endif()
set(GLAD_VERSION_STR \"${GLAD_VERSION}\")

project(glad
    DESCRIPTION "A header-only library with some useful algorithms for asio"
    HOMEPAGE_URL https://github.com/jgaa/glad
    VERSION ${GLAD_VERSION}
    LANGUAGES CXX)
set(GLAD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

option(GLAD_WITH_TESTS "Enable Tests" ON)
option(GLAD_WITH_EXAMPLES "Enable Examples" ON)
option(GLAD_USE_STD_UNORDERED_MAP "Use std::unordered_map for the cache" OFF)
option(GLAD_USE_BOOST_FLAT_UNORDERED_MAP "Use boost::unordered_flat_map for the cache" OFF)

if (GLAD_USE_STD_UNORDERED_MAP AND GLAD_USE_BOOST_FLAT_UNORDERED_MAP)
    message(FATAL_ERROR "You can use GLAD_USE_STD_UNORDERED_MAP or GLAD_USE_BOOST_FLAT_UNORDERED_MAP")
endif()

if (GLAD_USE_STD_UNORDERED_MAP)
    set(JGAA_GLAD_MAP_TYPE std::unordered_map)
endif()

if (GLAD_USE_BOOST_FLAT_UNORDERED_MAP)
    set(JGAA_GLAD_MAP_TYPE boost::unordered_flat_map)
endif()

if (GLAD_WITH_TESTS)
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
    #include/glad/AsyncCache.hpp
)

target_include_directories( ${PROJECT_NAME} INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated-include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(${PROJECT_NAME} INTERFACE cxx_std_20)

# Create an alias for external consumption
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

CONFIGURE_FILE(${PROJECT_SOURCE_DIR}/cmake/config.h.template ${CMAKE_BINARY_DIR}/generated-include/${PROJECT_NAME}/config.h)


if (GLAD_WITH_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if (GLAD_WITH_EXAMPLES)
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

install(EXPORT ${PROJECT_NAME}_Targets
        FILE ${PROJECT_NAME}Targets.cmake
        NAMESPACE ${PROJECT_NAME}::
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}/cmake)

install(FILES "${PROJECT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
              "${PROJECT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}/cmake)

install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/${PROJECT_NAME} DESTINATION include)
install(DIRECTORY ${CMAKE_BINARY_DIR}/generated-include/${PROJECT_NAME}/ DESTINATION include/${PROJECT_NAME})
