cmake_minimum_required(VERSION 3.25)

project(
        ItemBasedTempering
        VERSION 1.3.6
        LANGUAGES CXX)
set(PROJECT_LICENSE "The project license")
set(PROJECT_FRIENDLY_NAME ${PROJECT_NAME})
set(PROJECT_LONG_NAME "Item Based Tempering")
set(PROJECT_DESCRIPTION ${PROJECT_LONG_NAME})
set(PROJECT_COPYRIGHT "Copyright")

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(GNUInstallDirs)
include(addpluginsources)
include(pluginconfig)
include(plugintarget)
include(pluginpackage)
cmake_language(DEFER CALL include pluginbuildoptions)
include("${PROJECT_SOURCE_DIR}/local.cmake" OPTIONAL)

######## dependencies
set(Boost_NO_WARN_NEW_VERSIONS 1)
set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_MULTITHREADED  ON)
find_package(Boost CONFIG COMPONENTS)
if(Boost_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${Boost_LIBRARIES})
    target_include_directories(${PROJECT_NAME} PRIVATE ${Boost_INCLUDE_DIRS})
endif()

find_library(DETOURS_LIBRARY detours
    PATHS "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/lib"
    NO_DEFAULT_PATH
)

if(DETOURS_LIBRARY)
    message("-- Found detours")
    target_include_directories(${PROJECT_NAME} PRIVATE ${DETOURS_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${DETOURS_LIBRARY})
else()
    message("-- Could NOT find detours")
endif()
####################