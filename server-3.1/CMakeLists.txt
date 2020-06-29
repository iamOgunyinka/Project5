cmake_minimum_required(VERSION 3.0.0 FATAL_ERROR)

# Project
get_filename_component(PROJECT_DIR "${CMAKE_CURRENT_SOURCE_DIR}" ABSOLUTE)
set(Boost_USE_STATIC_LIBS OFF) 
set(Boost_USE_MULTITHREADED ON)  
set(Boost_USE_STATIC_RUNTIME OFF)
set(CMAKE_CXX_COMPILER "g++-8") 
find_package(Boost 1.72.0 COMPONENTS)

if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS}) 
endif()

set(DEPENDENCIES_DIR ${PROJECT_DIR}/dependencies)
set(PROJECT_NAME woody3)

include_directories(/root/woody/third-party/)
include_directories(${PROJECT_DIR}/include/)
link_directories(/usr/lib)
link_libraries(pthread ssl crypto odbc z stdc++fs zip)

# Outputs
set(OUTPUT_DEBUG ${PROJECT_DIR}/bin)
set(OUTPUT_RELEASE ${PROJECT_DIR}/bin)

################# CMake Project ############################
# The main options of project                              #
############################################################

project(${PROJECT_NAME} CXX)

# Define Release by default.
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release")
  message(STATUS "Build type not specified: Use Release by default.")
endif(NOT CMAKE_BUILD_TYPE)

############## Artefacts Output ############################
# Defines outputs , depending BUILD TYPE                   #
############################################################

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_DIR}/${OUTPUT_DEBUG}")
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_DIR}/${OUTPUT_DEBUG}")
  set(CMAKE_EXECUTABLE_OUTPUT_DIRECTORY "${PROJECT_DIR}/${OUTPUT_DEBUG}")
else()
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_DIR}/${OUTPUT_RELEASE}")
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_DIR}/${OUTPUT_RELEASE}")
  set(CMAKE_EXECUTABLE_OUTPUT_DIRECTORY "${PROJECT_DIR}/${OUTPUT_RELEASE}")
endif()

# Messages
message("${PROJECT_NAME}: MAIN PROJECT: ${CMAKE_PROJECT_NAME}")
message("${PROJECT_NAME}: CURR PROJECT: ${CMAKE_CURRENT_SOURCE_DIR}")
message("${PROJECT_NAME}: CURR BIN DIR: ${CMAKE_CURRENT_BINARY_DIR}")

# target_link_libraries(${PROJECT_NAME} OpenSSL::SSL)

############### Files & Targets ############################
# Files of project and target to build                     #
############################################################

# Source Files
set(SRC_FILES
    ./src/auto_home_http_sock.cpp
    ./src/auto_home_socks5_sock.cpp
    ./src/backgroundworker.cpp
    ./src/chinese_macau_socket.cpp
    ./src/database_connector.cpp
    ./src/grand_lisboa_socket.cpp
    ./src/jjgames_socket.cpp
    ./src/lisboa_macau_socket.cpp
    ./src/macau_baccarat_socket.cpp
    ./src/number_stream.cpp
    ./src/pc_auto_socket.cpp
    ./src/pp_sports.cpp
    ./src/qunar_socket.cpp
    ./src/safe_proxy.cpp
    ./src/server.cpp
    ./src/session.cpp
    ./src/sun_city_socket.cpp
    ./src/utilities.cpp
    ./src/wines_socket.cpp
    ./src/xpuji_socket.cpp
    ./src/worker.cpp
    ./src/sockets_instantiator.cpp
    ./src/socks_initiator.cpp
    ./main.cpp
)
source_group("Sources" FILES ${SRC_FILES})

# Header Files
set(HEADERS_FILES
    ./include/auto_home_http_sock.hpp
    ./include/auto_home_socks5_sock.hpp
    ./include/backgroundworker.hpp
    ./include/custom_timed_socket.hpp
    ./include/database_connector.hpp
    ./include/fields_alloc.hpp
    ./include/http_socket_base.hpp
    ./include/jjgames_socket.hpp
    ./include/number_stream.hpp
    ./include/pp_sports.hpp
    ./include/protocol.hpp
    ./include/qunar_socket.hpp
    ./include/safe_proxy.hpp
    ./include/server.hpp
    ./include/session.hpp
    ./include/sockets_interface.hpp
    ./include/socks5_http_socket_base.hpp
    ./include/socks5_https_socket_base.hpp
    ./include/utilities.hpp
    ./include/worker.hpp
)
source_group("Headers" FILES ${HEADERS_FILES})

# Add executable to build.
add_executable(${PROJECT_NAME} 
   ${SRC_FILES} ${HEADERS_FILES}
)

######################### Flags ############################
# Defines Flags for Windows and Linux                      #
############################################################

# final migration to C++17 from C++14
if(NOT MSVC)
   set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -O3")
   if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
       set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -O3")
   endif()
endif(NOT MSVC)

# Preprocessor definitions
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(${PROJECT_NAME} PRIVATE 
   -D_DEBUG 
   -D_CONSOLE 
    )
    if(MSVC)
        target_compile_options(${PROJECT_NAME} PRIVATE  /W3 /MD /Od /Zi /EHsc)
    endif()
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_definitions(${PROJECT_NAME} PRIVATE 
   -DNDEBUG 
   -D_CONSOLE 
    )
    if(MSVC)
        target_compile_options(${PROJECT_NAME} PRIVATE  /W3 /GL /Oi /Gy /Zi /EHsc)
    endif()
endif()

