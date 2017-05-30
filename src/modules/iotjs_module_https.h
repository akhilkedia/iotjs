/* Copyright 2017-present Samsung Electronics Co., Ltd. and other contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef IOTJS_MODULE_HTTPS_H
#define IOTJS_MODULE_HTTPS_H

#include "iotjs_def.h"
#include <uv.h>
#include <curl/curl.h>

typedef enum {
  HTTPS_GET = 0,
  HTTPS_POST,
  HTTPS_PUT,
  HTTPS_DELETE,
  HTTPS_HEAD,
  HTTPS_CONNECT,
  HTTPS_OPTIONS,
  HTTPS_TRACE
} HTTPS_Methods;

// A Per-Request Struct
typedef struct {
  // Original Request Details
  const char* URL;
  HTTPS_Methods method;
  struct curl_slist* header_list;
  // TLS certs Options
  const char* ca;
  const char* cert;
  const char* key;
  // Content-Length for Post and Put
  long content_length;

  // Handles
  uv_loop_t* loop;
  iotjs_jval_t jthis_native;
  CURLM* curl_multi_handle;
  uv_timer_t timeout;
  CURL* curl_easy_handle;
  // Curl Context
  uv_poll_t poll_handle;
  curl_socket_t sockfd;
  bool poll_handle_to_be_destroyed;
  int running_handles;
  bool request_done;

  // For SetTimeOut
  uv_timer_t socket_timeout;
  long timeout_ms;
  double last_bytes_num;
  uint64_t last_bytes_time;

  // For ReadData
  size_t cur_read_index;
  bool is_stream_writable;
  bool data_to_read;
  bool stream_ended;
  bool to_destroy_read_onwrite;
  iotjs_string_t read_chunk;
  iotjs_jval_t read_callback;
  iotjs_jval_t read_onwrite;
  uv_timer_t async_read_onwrite;

} IOTJS_VALIDATED_STRUCT(iotjs_https_t);

#endif /* IOTJS_MODULE_HTTPS_H */
