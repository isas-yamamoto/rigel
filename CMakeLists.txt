cmake_minimum_required(VERSION 3.10)
project(rigel VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)

# One object library so the shared and static builds don't compile rigel.cc
# twice.
add_library(rigel_obj OBJECT src/rigel.cc)
set_target_properties(rigel_obj PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(rigel_obj PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(rigel_obj PUBLIC Threads::Threads)
target_compile_options(rigel_obj PRIVATE -Wall)

add_library(rigel SHARED $<TARGET_OBJECTS:rigel_obj>)
target_include_directories(rigel PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(rigel PUBLIC Threads::Threads)
set_target_properties(rigel PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
)

add_library(rigel_static STATIC $<TARGET_OBJECTS:rigel_obj>)
target_include_directories(rigel_static PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(rigel_static PUBLIC Threads::Threads)
set_target_properties(rigel_static PROPERTIES OUTPUT_NAME rigel)

add_executable(rigel_cli src/rigel_cli.cc)
target_link_libraries(rigel_cli PRIVATE rigel)
set_target_properties(rigel_cli PROPERTIES OUTPUT_NAME rigel)

enable_testing()
add_executable(rigel_test src/test.cc)
target_link_libraries(rigel_test PRIVATE rigel Threads::Threads)
add_test(NAME rigel_test COMMAND rigel_test)

include(GNUInstallDirs)

install(TARGETS rigel rigel_static rigel_cli
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES src/rigel.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/rigel.pc.in
  ${CMAKE_CURRENT_BINARY_DIR}/rigel.pc
  @ONLY
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/rigel.pc
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)
