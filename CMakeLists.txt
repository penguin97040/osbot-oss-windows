cmake_minimum_required(VERSION 3.20)

# Bump the version here; it flows into the exe, the About box and version info.
project(osbot-oss-windows VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

enable_testing()

# ---- Protocol tests: platform independent, build anywhere (incl. Linux) ----
add_executable(protocol_tests tests/protocol_tests.cpp src/camera/ObsbotProtocol.cpp)
add_test(NAME protocol_tests COMMAND protocol_tests)
if(MSVC)
  target_compile_options(protocol_tests PRIVATE /W4 /utf-8)
  set_property(TARGET protocol_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
  target_compile_options(protocol_tests PRIVATE -Wall -Wextra)
  if(MINGW)
    target_link_options(protocol_tests PRIVATE -static)
  endif()
endif()

if(NOT WIN32)
  message(STATUS "Not targeting Windows: only building protocol_tests. "
                 "Use -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake to cross-compile the app.")
  return()
endif()

enable_language(RC)

# ---- The app: one self-contained exe ----
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
configure_file(src/version.h.in ${CMAKE_BINARY_DIR}/generated/version.h @ONLY)

add_executable(osbot-oss-windows WIN32
  src/main.cpp
  src/app/App.cpp
  src/app/Log.cpp
  src/app/Settings.cpp
  src/camera/CameraDevice.cpp
  src/camera/CameraWorker.cpp
  src/camera/ObsbotProtocol.cpp
  src/preview/Preview.cpp
  res/app.rc
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
  ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

target_include_directories(osbot-oss-windows PRIVATE
  src
  res
  ${CMAKE_BINARY_DIR}/generated
  ${IMGUI_DIR}
  ${IMGUI_DIR}/backends
)

target_compile_definitions(osbot-oss-windows PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00
  IMGUI_DISABLE_OBSOLETE_FUNCTIONS
)

target_link_libraries(osbot-oss-windows PRIVATE
  d3d11 d3dcompiler dxgi dwmapi
  ole32 oleaut32 uuid strmiids
  mfplat mfreadwrite mf mfuuid
  shell32 imm32
)

if(MSVC)
  # Static CRT so the exe runs without the Visual C++ Redistributable.
  set_property(TARGET osbot-oss-windows PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_compile_options(osbot-oss-windows PRIVATE /W4 /utf-8 /permissive- /wd4100)
  target_compile_definitions(osbot-oss-windows PRIVATE _CRT_SECURE_NO_WARNINGS)
  # The manifest is embedded via res/app.rc.
  target_link_options(osbot-oss-windows PRIVATE /MANIFEST:NO)
else()
  target_compile_options(osbot-oss-windows PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
  # Fully static: no libstdc++/libgcc/winpthread DLLs next to the exe.
  target_link_options(osbot-oss-windows PRIVATE -static -static-libgcc -static-libstdc++ -s)
endif()
