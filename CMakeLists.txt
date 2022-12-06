cmake_minimum_required(VERSION 3.20)
project(Rotator-Bridge CXX)

set (CMAKE_CXX_STANDARD 17)

add_executable(RBridge
  "src/rotators/CamPTZ.cpp"
  "src/rotators/rotctld.cpp"
  "src/main.cpp"
)

target_include_directories(RBridge PRIVATE ${CMAKE_SOURCE_DIR}/include)

if(WIN32)
  target_compile_definitions(RBridge PRIVATE WIN32)
  target_link_libraries(RBridge wsock32 ws2_32)
endif()