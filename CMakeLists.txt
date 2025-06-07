cmake_minimum_required(VERSION 3.24)

add_library(cbor-walker INTERFACE)
set_target_properties(cbor-walker PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/include)