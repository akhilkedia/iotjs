# Copyright 2017-present Samsung Electronics Co., Ltd. and other contributors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 2.8)
# Configure external libcurl
set(DEPS_CURL deps/libcurl)
set(DEPS_CURL_SRC ${ROOT_DIR}/${DEPS_CURL}/)

# TODO: cross-compiling - set root path or ensure no find_packages are being used.
# TODO: 127 and 99 are error codes

ExternalProject_Add(libcurl
  PREFIX ${DEPS_CURL}
  SOURCE_DIR ${DEPS_CURL_SRC}
  BUILD_IN_SOURCE 0
  BINARY_DIR ${DEPS_CURL}
  INSTALL_COMMAND
    ${CMAKE_COMMAND} -E copy
    ${CMAKE_BINARY_DIR}/${DEPS_CURL}/lib/libcurl.a
    ${CMAKE_BINARY_DIR}/lib/
  CMAKE_ARGS
    -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    -DCMAKE_C_FLAGS=${CMAKE_C_FLAGS}
    -DOS=${TARGET_OS}
  CMAKE_CACHE_ARGS
    -DHAVE_FSETXATTR_5:STRING=127
    -DHAVE_FSETXATTR_5__TRYRUN_OUTPUT:STRING=""
    -DHAVE_GLIBC_STRERROR_R:STRING=99
    -DHAVE_GLIBC_STRERROR_R__TRYRUN_OUTPUT:STRING=""
    -DHAVE_POSIX_STRERROR_R:STRING=0
    -DHAVE_POSIX_STRERROR_R__TRYRUN_OUTPUT:STRING=""
    -DHAVE_POLL_FINE_EXITCODE:STRING=0
    -DBUILD_CURL_EXE:BOOL=OFF
    -DCURL_STATICLIB:BOOL=ON
    -DHTTP_ONLY:BOOL=ON
    -DCURL_DISABLE_COOKIES:BOOL=ON
    -DCURL_DISABLE_CRYPTO_aUTH:BOOL=ON
    -DCMAKE_USE_OPENSSL:BOOL=OFF
    -DCURL_ZLIB:BOOL=OFF
    -DCURL_CA_PATH:STRING=none
)
add_library(curl STATIC IMPORTED)
add_dependencies(curl libcurl)
set_property(TARGET curl PROPERTY
  IMPORTED_LOCATION ${CMAKE_BINARY_DIR}/lib/libcurl.a)
set_property(DIRECTORY APPEND PROPERTY
  ADDITIONAL_MAKE_CLEAN_FILES ${CMAKE_BINARY_DIR}/lib/libcurl.a)
set(CURL_INCLUDE_DIR ${DEPS_CURL_SRC}/include/curl)
set(CURL_LIBS curl)
