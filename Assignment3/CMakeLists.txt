cmake_minimum_required(VERSION 3.10)
project(LoopPassProject)

# Cerca LLVM (senza forzare il linking)
find_package(LLVM REQUIRED CONFIG)

message(STATUS "Trovato LLVM ${LLVM_PACKAGE_VERSION}")

# Imposta gli include e i flag
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Crea il modulo
add_library(LoopPass MODULE src/LoopPass.cpp)
add_library(LICMotionPass MODULE src/LICMotion.cpp)
set_target_properties(LoopPass PROPERTIES PREFIX "")
