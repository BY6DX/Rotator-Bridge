cmake_minimum_required(VERSION 3.20)
project(Rotator-Bridge CXX)

add_executable(RBridge
  "src/rotators/Cam.cpp"
  "src/rotators/HamLib.cpp"

  "src/main.cpp"
)

target_include_directories(RBridge PRIVATE ${CMAKE_SOURCE_DIR}/include)