cmake_minimum_required(VERSION 3.5)
project(pancha)

###############################################################################
# Configuring C++ standard version
###############################################################################

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

###############################################################################
# Configuring C standard version
###############################################################################

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED True)
set(CMAKE_C_EXTENSIONS True)

###############################################################################
# Setting some complation flags
###############################################################################


add_compile_options(-m64 -O3 -march=native -g -Werror -Wfatal-errors)

###############################################################################
# Setting output targets
###############################################################################

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

###############################################################################
# Getting the modules
###############################################################################

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

###############################################################################
# DPDK
###############################################################################

find_package(DPDK REQUIRED)

message(DPDK_INCLUDE_DIRS="${DPDK_INCLUDE_DIRS}")
message(DPDK_LIBRARY_DIRS="${DPDK_LIBRARY_DIRS}")

include_directories(${DPDK_INCLUDE_DIRS})
link_directories(${DPDK_LIBRARY_DIRS})

###############################################################################
# Getting the lib source files
###############################################################################

file(GLOB_RECURSE SOURCES ${PROJECT_SOURCE_DIR}/lib/*.c)

###############################################################################
# Building each NF
###############################################################################

file(GLOB_RECURSE NFS ${PROJECT_SOURCE_DIR}/executables/*.c)
foreach(NF ${NFS})
    get_filename_component(NF_NAME ${NF} NAME_WE)
    set(NF_SOURCES ${NF} ${SOURCES})
    add_executable(${NF_NAME} ${NF_SOURCES})

    # https://stackoverflow.com/questions/56381463/executable-missing-full-library-path-after-build
    # cmake black magic...
    target_link_options(${NF_NAME} PUBLIC "-Wl,--disable-new-dtags")
    
    target_link_libraries(${NF_NAME} ${DPDK_LIBRARIES})
endforeach(NF ${NFS})
