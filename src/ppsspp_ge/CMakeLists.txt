# PPSSPP remains an external, pinned source tree. Only its GE is invoked.
# Core supplies timing/memory/platform dependencies, not CPU execution here.
# Avoid spending the one-time build optimizing its unused CPU recompilers.
if(NOT MSVC)
    target_compile_options(Core PRIVATE -O0)
endif()
add_library(repops-ge-bridge STATIC bridge.cpp platform.cpp)
if(APPLE)
    # Upstream attaches this platform dependency to its frontends, not Common.
    target_sources(repops-ge-bridge PRIVATE "${CMAKE_SOURCE_DIR}/Common/Render/Text/draw_text_cocoa.mm")
    set_source_files_properties("${CMAKE_SOURCE_DIR}/Common/Render/Text/draw_text_cocoa.mm"
        PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    find_library(REPOPS_CORETEXT CoreText REQUIRED)
    find_library(REPOPS_COREGRAPHICS CoreGraphics REQUIRED)
    target_link_libraries(repops-ge-bridge PUBLIC ${REPOPS_CORETEXT} ${REPOPS_COREGRAPHICS})
endif()
target_include_directories(repops-ge-bridge PUBLIC "${CMAKE_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/Common" "${CMAKE_BINARY_DIR}")
target_link_libraries(repops-ge-bridge PUBLIC Core ${COCOA_LIBRARY} ${QUARTZ_CORE_LIBRARY} ${IOKIT_LIBRARY})
target_compile_features(repops-ge-bridge PUBLIC cxx_std_17)
add_executable(repops-ge-smoke smoke.cpp)
target_link_libraries(repops-ge-smoke PRIVATE repops-ge-bridge)

# Compile the same reviewed C as the ordinary host binary. Only platform GE
# services use the C++ bridge; Unicorn still executes POPS-generated CPU code.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${REPOPS_ROOT}/Makefile")
file(STRINGS "${REPOPS_ROOT}/Makefile" native_lines REGEX "^NATIVE_SRC")
string(REGEX MATCHALL "src/[A-Za-z0-9_/]+\\.c" native_files "${native_lines}")
list(TRANSFORM native_files PREPEND "${REPOPS_ROOT}/")
execute_process(COMMAND "${REPOPS_ROOT}/.tools/verify-env/bin/python" -c
    "import pathlib,unicorn;print(pathlib.Path(unicorn.__file__).parent)"
    OUTPUT_VARIABLE UNICORN_ROOT OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
add_executable(repops-native-ge ${native_files} native_adapter.c)
target_compile_definitions(repops-native-ge PRIVATE REPOPS_WITH_PPSSPP_GE=1)
target_include_directories(repops-native-ge PRIVATE "${UNICORN_ROOT}/include" ${PNG_INCLUDE_DIRS})
target_link_libraries(repops-native-ge PRIVATE repops-ge-bridge "${UNICORN_ROOT}/lib/libunicorn.a" ${PNG_LIBRARIES} z)
