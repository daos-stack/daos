cmake_minimum_required(VERSION 2.8.12.2...3.15 FATAL_ERROR)
project(MercuryExamples C)

#------------------------------------------------------------------------------
# Setup cmake module
#------------------------------------------------------------------------------
set(MercuryExamples_CMAKE_DIR "${MercuryExamples_SOURCE_DIR}/CMake")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${MercuryExamples_CMAKE_DIR})

#------------------------------------------------------------------------------
# Add src
#------------------------------------------------------------------------------
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src)
