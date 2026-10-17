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
target_link_libraries(FlareSimOFX PRIVATE FlareSimOptics)
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
  target_compile_options(FlareSimOFX PRIVATE /utf-8 /W4)
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
