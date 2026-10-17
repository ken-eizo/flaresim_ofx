cmake_minimum_required(VERSION 3.24)
project(FlareSimOFX VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()

set(NUKE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/vendor/nuke/src")
set(OFX_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/openfx/include")

add_library(FlareSimOptics STATIC
  ${NUKE_SRC}/lens.cpp
  ${NUKE_SRC}/trace.cpp
  ${NUKE_SRC}/ghost.cpp
  ${NUKE_SRC}/starburst.cpp
  engine/FlareLensCore.cpp
  engine/FlareLensOptics.cpp
  engine/FlareLensCpuParallel.cpp
  src/SourceDetection.cpp)
target_include_directories(FlareSimOptics PUBLIC engine src ${NUKE_SRC})
if(MSVC)
  target_compile_options(FlareSimOptics PRIVATE /utf-8 /W3)
endif()

add_library(FlareSimOFX MODULE src/FlareSimOFX.cpp)
target_include_directories(FlareSimOFX PRIVATE ${OFX_HEADERS})
if(WIN32 OR APPLE)
  target_sources(FlareSimOFX PRIVATE src/FlareSimOverlay.cpp)
  target_compile_definitions(FlareSimOFX PRIVATE FLARESIM_HAVE_OVERLAY=1)
  if(WIN32)
    target_link_libraries(FlareSimOFX PRIVATE opengl32)
  else()
    find_library(FLARESIM_OPENGL OpenGL REQUIRED)
    target_link_libraries(FlareSimOFX PRIVATE "${FLARESIM_OPENGL}")
  endif()
endif()
target_link_libraries(FlareSimOFX PRIVATE FlareSimOptics)
# GPU is optional. The plugin NEVER requires CUDA or Metal from the host.
# The standard OFX image path stays CPU-addressable; optical ray tracing can
# be offloaded internally and copied back. No unsupported OFX GPU flags.
set(FLARESIM_GPU "AUTO" CACHE STRING "GPU backend: AUTO, ON, OFF")
set_property(CACHE FLARESIM_GPU PROPERTY STRINGS AUTO ON OFF)
set(FLARESIM_GPU_ENABLED OFF)
if(NOT FLARESIM_GPU STREQUAL "OFF" AND WIN32)
  include(CheckLanguage)
  check_language(CUDA)
  if(CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    find_package(CUDAToolkit REQUIRED)
    set(FLARESIM_GPU_ENABLED ON)
    target_sources(FlareSimOFX PRIVATE gpu/FlareLensGpu.cu gpu/FlareSimCuda.cu)
    target_include_directories(FlareSimOFX PRIVATE gpu)
    target_compile_definitions(FlareSimOFX PRIVATE FLARESIM_HAVE_CUDA=1)
    target_link_libraries(FlareSimOFX PRIVATE CUDA::cudart_static)
    set_target_properties(FlareSimOFX PROPERTIES CUDA_STANDARD 17 CUDA_ARCHITECTURES "75;86;89")
    message(STATUS "FlareSim OFX: NVIDIA CUDA optical backend enabled")
  endif()
elseif(NOT FLARESIM_GPU STREQUAL "OFF" AND APPLE)
  enable_language(OBJCXX)
  find_library(FLARESIM_METAL_FRAMEWORK Metal REQUIRED)
  find_library(FLARESIM_FOUNDATION_FRAMEWORK Foundation REQUIRED)
  find_program(FLARESIM_XCRUN xcrun REQUIRED)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  set(FLARESIM_GPU_ENABLED ON)
  target_sources(FlareSimOFX PRIVATE gpu/FlareLensMetal.mm gpu/FlareSimMetalBridge.mm)
  target_include_directories(FlareSimOFX PRIVATE gpu)
  target_compile_definitions(FlareSimOFX PRIVATE FLARESIM_HAVE_METAL=1)
  target_compile_options(FlareSimOFX PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
  target_link_libraries(FlareSimOFX PRIVATE
    "${FLARESIM_METAL_FRAMEWORK}" "${FLARESIM_FOUNDATION_FRAMEWORK}")
  set(FLARESIM_METAL_PHYSICS "${CMAKE_BINARY_DIR}/generated/FlareLensMetalPhysics.metal")
  set(FLARESIM_METAL_AIR "${CMAKE_BINARY_DIR}/generated/FlareLensMetal.air")
  set(FLARESIM_METAL_LIBRARY "${CMAKE_BINARY_DIR}/generated/FlareLensMetal.metallib")
  add_custom_command(OUTPUT "${FLARESIM_METAL_PHYSICS}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_metal_physics.py"
      --cuda "${CMAKE_CURRENT_SOURCE_DIR}/gpu/FlareLensGpu.cu"
      --output "${FLARESIM_METAL_PHYSICS}"
    DEPENDS scripts/generate_metal_physics.py gpu/FlareLensGpu.cu VERBATIM)
  add_custom_command(OUTPUT "${FLARESIM_METAL_AIR}"
    COMMAND "${FLARESIM_XCRUN}" -sdk macosx metal
      -std=macos-metal2.3 -I "${CMAKE_BINARY_DIR}/generated" -c
      "${CMAKE_CURRENT_SOURCE_DIR}/gpu/FlareLensMetalKernels.metal"
      -o "${FLARESIM_METAL_AIR}"
    DEPENDS "${FLARESIM_METAL_PHYSICS}" gpu/FlareLensMetalKernels.metal VERBATIM)
  add_custom_command(OUTPUT "${FLARESIM_METAL_LIBRARY}"
    COMMAND "${FLARESIM_XCRUN}" -sdk macosx metallib
      "${FLARESIM_METAL_AIR}" -o "${FLARESIM_METAL_LIBRARY}"
    DEPENDS "${FLARESIM_METAL_AIR}" VERBATIM)
  add_custom_target(FlareSimMetalShaders ALL DEPENDS "${FLARESIM_METAL_LIBRARY}")
  add_dependencies(FlareSimOFX FlareSimMetalShaders)
  message(STATUS "FlareSim OFX: Apple Metal optical backend enabled")
endif()
if(NOT FLARESIM_GPU_ENABLED)
  if(FLARESIM_GPU STREQUAL "ON")
    message(FATAL_ERROR "GPU backend was required but not available on this toolchain")
  endif()
  target_sources(FlareSimOFX PRIVATE gpu/FlareSimNoGpu.cpp)
  target_include_directories(FlareSimOFX PRIVATE gpu)
  message(STATUS "FlareSim OFX: CPU-only build")
endif()

if(WIN32)
  set(FLARESIM_OS_FOLDER "Win64")
elseif(APPLE)
  set(FLARESIM_OS_FOLDER "MacOS")
else()
  set(FLARESIM_OS_FOLDER "Linux-x86-64")
endif()
set(FLARESIM_BUNDLE_ROOT "${CMAKE_BINARY_DIR}/FlareSim.ofx.bundle/Contents")
set_target_properties(FlareSimOFX PROPERTIES
  PREFIX "" OUTPUT_NAME "FlareSim" SUFFIX ".ofx"
  LIBRARY_OUTPUT_DIRECTORY "${FLARESIM_BUNDLE_ROOT}/${FLARESIM_OS_FOLDER}"
  RUNTIME_OUTPUT_DIRECTORY "${FLARESIM_BUNDLE_ROOT}/${FLARESIM_OS_FOLDER}"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${FLARESIM_BUNDLE_ROOT}/${FLARESIM_OS_FOLDER}"
  LIBRARY_OUTPUT_DIRECTORY_RELEASE "${FLARESIM_BUNDLE_ROOT}/${FLARESIM_OS_FOLDER}"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${FLARESIM_BUNDLE_ROOT}/${FLARESIM_OS_FOLDER}"
  LIBRARY_OUTPUT_DIRECTORY_DEBUG "${FLARESIM_BUNDLE_ROOT}/${FLARESIM_OS_FOLDER}")
if(MSVC)
  target_compile_options(FlareSimOFX PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8> $<$<COMPILE_LANGUAGE:CXX>:/W4>)
endif()
if(UNIX AND NOT APPLE)
  target_link_libraries(FlareSimOFX PRIVATE dl)
endif()

# An installed OFX bundle is self-contained: no absolute build paths, no Nuke/AE SDK.
# Keep the original upstream .lens bytes and manifest.
file(MAKE_DIRECTORY "${FLARESIM_BUNDLE_ROOT}/Resources")
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/resources/lenses" DESTINATION "${FLARESIM_BUNDLE_ROOT}/Resources")
configure_file(resources/LensCatalog.txt "${FLARESIM_BUNDLE_ROOT}/Resources/LensCatalog.txt" COPYONLY)
configure_file(LICENSE "${FLARESIM_BUNDLE_ROOT}/Resources/LICENSE.txt" COPYONLY)
configure_file(NOTICE.md "${FLARESIM_BUNDLE_ROOT}/Resources/NOTICE.md" COPYONLY)
if(APPLE AND FLARESIM_GPU_ENABLED)
  add_custom_command(TARGET FlareSimOFX POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${FLARESIM_METAL_LIBRARY}"
      "${FLARESIM_BUNDLE_ROOT}/Resources/FlareLensMetal.metallib")
endif()

if(APPLE)
  file(WRITE "${CMAKE_BINARY_DIR}/FlareSim.Info.plist"
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?><!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\"><plist version=\"1.0\"><dict><key>CFBundleExecutable</key><string>FlareSim.ofx</string><key>CFBundleIdentifier</key><string>dev.keneizo.flaresim.ofx</string><key>CFBundleName</key><string>FlareSim</string><key>CFBundlePackageType</key><string>BNDL</string></dict></plist>")
  configure_file("${CMAKE_BINARY_DIR}/FlareSim.Info.plist" "${FLARESIM_BUNDLE_ROOT}/Info.plist" COPYONLY)
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(FlareSimCoreTest tests/core_test.cpp)
  target_link_libraries(FlareSimCoreTest PRIVATE FlareSimOptics)
  target_compile_definitions(FlareSimCoreTest PRIVATE FLARESIM_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME FlareSimCoreTest COMMAND FlareSimCoreTest)
  add_executable(FlareSimCatalogTest tests/catalog_test.cpp)
  target_link_libraries(FlareSimCatalogTest PRIVATE FlareSimOptics)
  target_compile_definitions(FlareSimCatalogTest PRIVATE FLARESIM_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
  if(MSVC)
    target_compile_options(FlareSimCatalogTest PRIVATE /utf-8)
    target_compile_options(FlareSimCoreTest PRIVATE /utf-8)
  endif()
  add_test(NAME FlareSimCatalogTest COMMAND FlareSimCatalogTest)
  add_executable(FlareSimOFXHostTest tests/ofx_host_test.cpp)
  target_include_directories(FlareSimOFXHostTest PRIVATE ${OFX_HEADERS})
  target_compile_definitions(FlareSimOFXHostTest PRIVATE FLARESIM_BUNDLE_BINARY="$<TARGET_FILE:FlareSimOFX>")
  add_dependencies(FlareSimOFXHostTest FlareSimOFX)
  if(UNIX AND NOT APPLE)
    target_link_libraries(FlareSimOFXHostTest PRIVATE dl)
  endif()
  add_test(NAME FlareSimOFXHostTest COMMAND FlareSimOFXHostTest)
  add_executable(FlareSimOFXEntryTest tests/ofx_entry_test.cpp)
  target_include_directories(FlareSimOFXEntryTest PRIVATE ${OFX_HEADERS})
  target_compile_definitions(FlareSimOFXEntryTest PRIVATE FLARESIM_BUNDLE_BINARY="$<TARGET_FILE:FlareSimOFX>")
  add_dependencies(FlareSimOFXEntryTest FlareSimOFX)
  if(UNIX AND NOT APPLE)
    target_link_libraries(FlareSimOFXEntryTest PRIVATE dl)
  endif()
  add_test(NAME FlareSimOFXEntryTest COMMAND FlareSimOFXEntryTest)
endif()

install(DIRECTORY "${CMAKE_BINARY_DIR}/FlareSim.ofx.bundle"
        DESTINATION "OFX/Plugins")
