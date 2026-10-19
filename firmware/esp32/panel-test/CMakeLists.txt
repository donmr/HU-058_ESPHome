# ESP-IDF project file. Builds the same src/main.cpp as platformio.ini, with
# the Arduino core pulled in as an IDF component (see main/idf_component.yml).
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(panel-test)
