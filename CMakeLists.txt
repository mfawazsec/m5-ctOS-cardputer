cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(EXTRA_COMPONENT_DIRS components)

project(m5-ctOS)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)
