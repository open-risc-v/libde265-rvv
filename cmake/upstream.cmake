# 上游接入：libde265（cmake）→ <build>/pkg/prefix。

set(PKG_ROOT "${CMAKE_BINARY_DIR}/pkg")
if(NOT DEFINED PKG_PREFIX)
  set(PKG_PREFIX "${PKG_ROOT}/prefix")
  add_custom_target(pkg ALL
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/cmake/pkg-build.sh" "${SRC_DIR}" "${PKG_ROOT}"
    COMMENT "构建上游 libde265（${SRC_DIR}）"
    VERBATIM)
  set(HAVE_PKG_TARGET TRUE)
endif()

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/out")
add_executable(libde265_harness test/pkg_libde265.c)
if(HAVE_PKG_TARGET)
  add_dependencies(libde265_harness pkg)
endif()
target_include_directories(libde265_harness PRIVATE "${PKG_PREFIX}/include")
target_link_directories(libde265_harness PRIVATE "${PKG_PREFIX}/lib")
# libde265 内部有 C++（nal-parser 等）：C 探针必须用 C++ 驱动链接，不能靠 -lstdc++
set_target_properties(libde265_harness PROPERTIES LINKER_LANGUAGE CXX)
target_link_libraries(libde265_harness PRIVATE de265 pthread)
target_compile_options(libde265_harness PRIVATE ${RVV_FLAGS})
