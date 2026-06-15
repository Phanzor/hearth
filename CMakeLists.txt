cmake_minimum_required(VERSION 3.20)
project(hearth CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
  GIT_TAG v7.0.0)
FetchContent_Declare(json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
FetchContent_Declare(httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.18.1)

# FTXUI pulls in examples/docs by default; we only need the libraries.
set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(ftxui json httplib)

find_package(Threads REQUIRED)

add_executable(hearth
  src/main.cpp
  src/ui.cpp
  src/ollama.cpp
  src/config.cpp
  src/theme.cpp
  src/markdown.cpp
  src/storage.cpp)

target_include_directories(hearth PRIVATE src)
target_link_libraries(hearth PRIVATE
  ftxui::screen
  ftxui::dom
  ftxui::component
  nlohmann_json::nlohmann_json
  httplib::httplib
  Threads::Threads)
