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

#include "iotjs_module_https.h"
#include "iotjs_objectwrap.h"
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdlib.h>
#include <string.h>

static void iotjs_https_destroy(iotjs_https_t* https_data);
IOTJS_DEFINE_NATIVE_HANDLE_INFO(https);

iotjs_jval_t* iotjs_https_jthis_from_https(iotjs_https_t* https_data) {
  IOTJS_VALIDATED_STRUCT_METHOD(iotjs_https_t, https_data);
  return &(_this->jthis_native);
}

// Call any property of ClientRequest._Incoming
bool iotjs_https_jcallback(iotjs_https_t* https_data, const char* property,
                           const iotjs_jargs_t* jarg, bool resultvalue) {
  iotjs_jval_t* jthis = iotjs_https_jthis_from_https(https_data);
  bool retval = true;
  if (iotjs_jval_is_null(jthis))
    return retval;

  iotjs_jval_t jincoming =
      iotjs_jval_get_property(jthis, IOTJS_MAGIC_STRING__INCOMING);
  iotjs_jval_t cb = iotjs_jval_get_property(&jincoming, property);

  IOTJS_ASSERT(iotjs_jval_is_function(&cb));
  printf("Invoking CallBack To JS %s\n", property);
  if (!resultvalue) {
    iotjs_make_callback(&cb, &jincoming, jarg);
  } else {
    iotjs_jval_t result =
        iotjs_make_callback_with_result(&cb, &jincoming, jarg);
    retval = iotjs_jval_as_boolean(&result);
    iotjs_jval_destroy(&result);
  }

  iotjs_jval_destroy(&jincoming);
  iotjs_jval_destroy(&cb);
  return retval;
}

// Constructor
iotjs_https_t* iotjs_https_create(const char* URL, const char* method,
                                  const char* ca, const char* cert,
                                  const char* key, const iotjs_jval_t* jthis) {
  iotjs_https_t* https_data = IOTJS_ALLOC(iotjs_https_t);
  IOTJS_VALIDATED_STRUCT_CONSTRUCTOR(iotjs_https_t, https_data);

  // Original Request Details
  printf("Saving URL in _this \n");
  _this->URL = URL;
  _this->header_list = NULL;
  if (strcmp(method, STRING_GET) == 0)
    _this->method = HTTPS_GET;
  else if (strcmp(method, STRING_POST) == 0)
    _this->method = HTTPS_POST;
  else if (strcmp(method, STRING_PUT) == 0)
    _this->method = HTTPS_PUT;
  else if (strcmp(method, STRING_DELETE) == 0)
    _this->method = HTTPS_DELETE;
  else if (strcmp(method, STRING_HEAD) == 0)
    _this->method = HTTPS_HEAD;
  else if (strcmp(method, STRING_CONNECT) == 0)
    _this->method = HTTPS_CONNECT;
  else if (strcmp(method, STRING_OPTIONS) == 0)
    _this->method = HTTPS_OPTIONS;
  else if (strcmp(method, STRING_TRACE) == 0)
    _this->method = HTTPS_TRACE;
  else {
    // Will never reach here cuz checked in JS
  }

  // TLS certs stuff
  _this->ca = ca;
  _this->cert = cert;
  _this->key = key;
  // Content Length stuff
  _this->content_length = -1;

  // Handles
  _this->loop = iotjs_environment_loop(iotjs_environment_get());
  _this->jthis_native = iotjs_jval_create_copied(jthis);
  iotjs_jval_set_object_native_handle(&(_this->jthis_native),
                                      (uintptr_t)https_data, &https_native_info);
  _this->curl_multi_handle = curl_multi_init();
  _this->curl_easy_handle = curl_easy_init();
  _this->timeout.data = (void*)https_data;
  uv_timer_init(_this->loop, &(_this->timeout));
  _this->poll_handle_to_be_destroyed = false;
  _this->request_done = false;

  // Timeout stuff
  _this->timeout_ms = -1;
  _this->last_bytes_num = -1;
  _this->last_bytes_time = 0;
  _this->socket_timeout.data = (void*)https_data;
  uv_timer_init(_this->loop, &(_this->socket_timeout));

  // ReadData stuff
  _this->cur_read_index = 0;
  _this->is_stream_writable = false;
  _this->stream_ended = false;
  _this->data_to_read = false;
  _this->to_destroy_read_onwrite = false;
  _this->async_read_onwrite.data = (void*)https_data;
  uv_timer_init(_this->loop, &(_this->async_read_onwrite));
  // No Need to read data for following types of requests
  if (_this->method == HTTPS_GET || _this->method == HTTPS_DELETE ||
      _this->method == HTTPS_HEAD || _this->method == HTTPS_OPTIONS ||
      _this->method == HTTPS_TRACE)
    _this->stream_ended = true;

  return https_data;
}

static void iotjs_https_destroy(iotjs_https_t* https_data) {
  IOTJS_VALIDATED_STRUCT_DESTRUCTOR(iotjs_https_t, https_data);
  // To shutup unused variable _this warning
  _this->URL = NULL;
  IOTJS_RELEASE(https_data);
  printf("destroyed native jthis\n");
}

void iotjs_https_uv_close_callback(uv_handle_t* handle) {
  printf("Entered iotjs_https_uv_close_callback \n");
  iotjs_https_t* https_data = (iotjs_https_t*)handle->data;
  printf("The address of https_data recieved is %p \n", (void*)https_data);
  IOTJS_VALIDATED_STRUCT_METHOD(iotjs_https_t, https_data);
  _this->closing_handles = _this->closing_handles - 1;
  if (_this->closing_handles == 0) {
    printf("Called jval destroy on this \n");
    iotjs_jval_destroy(&_this->jthis_native);
  }
}

//Cleanup before destructor
void iotjs_https_cleanup(iotjs_https_t* https_data) {
  printf("Cleaning up iotjs_https_t \n");
  printf("The address of https_data recieved is %p \n", (void*)https_data);
  IOTJS_VALIDATED_STRUCT_METHOD(iotjs_https_t, https_data);
  _this->loop = NULL;

  uv_close((uv_handle_t*)&_this->timeout,
           (uv_close_cb)iotjs_https_uv_close_callback);
  uv_close((uv_handle_t*)&_this->socket_timeout,
           (uv_close_cb)iotjs_https_uv_close_callback);
  uv_close((uv_handle_t*)&_this->async_read_onwrite,
           (uv_close_cb)iotjs_https_uv_close_callback);

  iotjs_https_jcallback(https_data, IOTJS_MAGIC_STRING_ONEND,
                        iotjs_jargs_get_empty(), false);
  iotjs_https_jcallback(https_data, IOTJS_MAGIC_STRING_ONCLOSED,
                        iotjs_jargs_get_empty(), false);

  curl_multi_remove_handle(_this->curl_multi_handle, _this->curl_easy_handle);
  curl_easy_cleanup(_this->curl_easy_handle);
  _this->curl_easy_handle = NULL;
  curl_multi_cleanup(_this->curl_multi_handle);
  _this->curl_multi_handle = NULL;

  if (_this->poll_handle_to_be_destroyed)
    _this->closing_handles = 4;
  else
    _this->closing_handles = 3;

  if (_this->poll_handle_to_be_destroyed) {
    printf("Stopping poll handle in iotjs_https_cleanup");
    uv_close((uv_handle_t*)&_this->poll_handle,
             (uv_close_cb)iotjs_https_uv_close_callback);
    _this->poll_handle_to_be_destroyed = false;
  }
  curl_slist_free_all(_this->header_list);

  // if (_this->data_to_read) {
  //  _this->data_to_read = false;
  //  printf("about to destroy read_chunk \n");
  //  iotjs_string_destroy(&(_this->read_chunk));
  //}

  if (_this->to_destroy_read_onwrite) {
    const iotjs_jargs_t* jarg = iotjs_jargs_get_empty();
    const iotjs_jval_t* jthis = &(_this->jthis_native);
    IOTJS_ASSERT(iotjs_jval_is_function(&(_this->read_onwrite)));

    printf("Invoking CallBack To JS read_callback\n");
    if (!iotjs_jval_is_undefined(&(_this->read_callback)))
      iotjs_make_callback(&(_this->read_callback), jthis, jarg);

    printf("Invoking CallBack To JS read_onwrite\n");
    iotjs_make_callback(&(_this->read_onwrite), jthis, jarg);
    printf("Exiting iotjs_https_call_read_onwrite\n");
    _this->to_destroy_read_onwrite = false;
    printf("Destroying read_onwrite \n");
    iotjs_string_destroy(&(_this->read_chunk));
    iotjs_jval_destroy(&(_this->read_onwrite));
    iotjs_jval_destroy(&(_this->read_callback));
  }
  // IOTJS_RELEASE(https_data);
  printf("Finished cleaning up iotjs_https_t \n");
  return;
}

// ------------Actual Functions ----------


// Call onWrite and callback after ClientRequest._write
void iotjs_https_call_read_onwrite(uv_timer_t* timer) {
  printf("Entered iotjs_https_call_read_onwrite\n");
  iotjs_https_t* https_data = (iotjs_https_t*)(timer->data);
  IOTJS_VALIDATED_STRUCT_METHOD(iotjs_https_t, https_data);

  uv_timer_stop(&(_this->async_read_onwrite));
  if (iotjs_jval_is_null(&_this->jthis_native))
    return;
  const iotjs_jargs_t* jarg = iotjs_jargs_get_empty();
  const iotjs_jval_t* jthis = &(_this->jthis_native);
  IOTJS_ASSERT(iotjs_jval_is_function(&(_this->read_onwrite)));

  printf("Invoking CallBack To JS read_callback\n");
  if (!iotjs_jval_is_undefined(&(_this->read_callback)))
    iotjs_make_callback(&(_this->read_callback), jthis, jarg);

  printf("Invoking CallBack To JS read_onwrite\n");
  iotjs_make_callback(&(_this->read_onwrite), jthis, jarg);
  printf("Exiting iotjs_https_call_read_onwrite\n");
}

// Call the above method Asynchronously
void iotjs_https_call_read_onwrite_async(iotjs_https_t* https_data) {
  IOTJS_VALIDATED_STRUCT_METHOD(iotjs_https_t, https_data);
  uv_timer_start(&(_this->async_read_onwrite), iotjs_https_call_read_onwrite, 0,
                 0);
  printf("In iotjs_https_call_read_onwrite_async \n");
}

//Socket Assigned Callback
int iotjs_https_curl_sockopt_callback(void* userp, curl_socket_t curlfd,
                                      curlsocktype purpose) {
  iotjs_https_t* https_data = (iotjs_https_t*)userp;
  iotjs_https_jcallback(https_data, IOTJS_MAGIC_STRING_ONSOCKET,
                        iotjs_jargs_get_empty(), false);
  return CURL_SOCKOPT_OK;
}

//Read callback is actually to write data to outgoing request
size_t iotjs_https_curl_read_callback(void* contents, size_t size, size_t nmemb,
                                      void* userp) {
  // printf("Entered iotjs_https_curl_read_callback \n");
  iotjs_https_t* https_data = (iotjs_https_t*)userp;
  IOTJS_VALIDATED_STRUCT_METHOD(iotjs_https_t, https_data);
  printf("Entered iotjs_https_curl_read_callback %zu \n",
         _this->cur_read_index);

  // If stream wasnt made writable yet, make it so.
  if (!_this->is_stream_writable) {
    _this->is_stream_writable = true;
    iotjs_https_jcallback(https_data, IOTJS_MAGIC_STRING_ONWRITABLE,
                          iotjs_jargs_get_empty(), false);
    printf("Made Stream Writeable!!! \n");
  }

  if (_this->data_to_read) {
    size_t real_size = size * nmemb;
    size_t chunk_size = iotjs_string_size(&(_this->read_chunk));
    size_t left_to_copy_size = chunk_size - _this->cur_read_index;

    if (real_size < 1)
      return 0;

    // send some data
    if (_this->cur_read_index < chunk_size) {
      size_t num_to_copy =
          (left_to_copy_size < real_size) ? left_to_copy_size : real_size;
      printf("in iotjs_https_curl_read_callback %zu  \n",
             _this->cur_read_index);
      const char* buf = iotjs_string_data(&(_this->read_chunk));
      buf = &buf[_this->cur_read_index];
      strncpy((char*)contents, buf, num_to_copy);
      _this->cur_read_index = _this->cur_read_index + num_to_copy;
      printf("***************** Wrote %zu bytes of data ******************\n",
             num_to_copy);
      return num_to_copy;
    }

    // Finished sending one chunk of data
    _this->cur_read_index = 0;
    _this->data_to_read = false;
    printf("about to destroy read_chunk \n");
    iotjs_https_call_read_onwrite_async(https_data);
  }

  // If the data is sent, and stream hasn't ended, wait for more data
  if (!_this->stream_ended) {
    printf("Pausing Read \n");
    return CURL_READFUNC_PAUSE;
  }

  // All done, end the transfer
  printf("Exiting iotjs_https_curl_read_callback Finally\n\n");
  return 0;
}

//Write Callback is actually to read data from incoming response
size_t iotjs_https_curl_write_callback(void* contents, size_t size,
                                       size_t nmemb, void* userp) {
  iotjs_https_t* https_data = (iotjs_https_t*)userp;
  IOTJS_VALIDATED_STRUCT_METHOD(iotjs_https_t, https_data);
  size_t real_size = size * nmemb;
  printf("Entered iotjs_https_curl_write_callback \n");

  printf("Here1 iotjs_https_curl_write_callback\n");
  if (iotjs_jval_is_null(&_this->jthis_native))
    return real_size - 1;
  iotjs_jargs_t jarg = iotjs_jargs_create(1);
  iotjs_jval_t jresult_arr = iotjs_jval_create_byte_array(real_size, contents);
  iotjs_string_t jresult_string =
      iotjs_string_create_with_size(contents, real_size);
  iotjs_jargs_append_string(&jarg, &jresult_string);
  // TODO: Use the jresult_arr Byte Array in production, but in testing use
  // string. Comment out above line.
  // iotjs_jargs_append_jval(&jarg, &jresult_arr);

  bool result =
      iotjs_https_jcallback(https_data, IOTJS_MAGIC_STRING_ONDATA, &jarg, true);

  iotjs_jval_destroy(&jresult_arr);
  iotjs_string_destroy(&jresult_string);
  iotjs_jargs_destroy(&jarg);
  printf("Exiting iotjs_https_curl_write_callback \n\n");

  if (!result) {
    return real_size - 1;
  }

  return real_size;
}

void iotjs_https_check_done(iotjs_https_t* https_data) {
  IOTJS_VALIDATED_STRUCT_METHOD(iotjs_https_t, https_data);
  char* done_url;
  CURLMsg* message;
  int pending;
  bool error = false;
  printf("In iotjs_https_check_done \n");

  while ((message = curl_multi_info_read(_this->curl_multi_handle, &pending))) {
    switch (message->msg) {
      case CURLMSG_DONE:
        curl_easy_getinfo(message->easy_handle, CURLINFO_EFFECTIVE_URL,
                          &done_url);
        printf("%s Request Done \n", done_url);
        break;
      default:
        error = true;
    }
    if (error) {
      iotjs_jargs_t jarg = iotjs_jargs_create(1);
      char error[] = "Unknown Error has occured.";
      iotjs_string_t jresult_string =
          iotjs_string_create_with_size(error, strlen(error));
      iotjs_jargs_append_string(&jarg, &jresult_string);
      iotjs_https_jcallback(https_data, IOTJS_MAGIC_STRING_ONERROR, &jarg,
                            false);
      iotjs_string_destroy(&jresult_string);
      iotjs_jargs_destroy(&jarg);
    }
    if (_this->stream_ended) {
      iotjs_https_cleanup(https_data);
    } else {
      printf("Marking request as Done. \n");
      if (_this->to_destroy_read_onwrite) {
        iotjs_https_call_read_onwrite_async(https_data);
      }
      _this->request_done = true;
    }
    break;
  }
}

void iotjs_https_uv_poll_callback(uv_poll_t* poll, int status, int events) {
  iotjs_https_t* https_data = (iotjs_https_t*)poll->data;
  IOTJS_VALIDATED_STRUCT_METHOD(iotjs_https_t, https_data);
  printf("Entered in iotjs_https_uv_poll_callback \n");

  int flags = 0;
  if (status < 0)
    flags = CURL_CSELECT_ERR;
  if (!status && events & UV_READABLE)
    flags |= CURL_CSELECT_IN;
  if (!status && events & UV_WRITABLE)
    flags |= CURL_CSELECT_OUT;

  int curlmcode =
      curl_multi_socket_action(_this->curl_multi_handle, _this->sockfd, flags,
                               &_this->running_handles);
  printf("The error code is %d \n", curlmcode);
  iotjs_https_check_done(https_data);
  printf("Leaving iotjs_https_uv_poll_callback Call \n");
}

// This function is for signalling to curl timeout has passed.
// This timeout is usually given by curl itself.
void iotjs_https_uv_timeout_callback(uv_timer_t* timer) {
  iotjs_https_t* https_data = (iotjs_https_t*)(timer->data);
  IOTJS_VALIDATED_STRUCT_METHOD(iotjs_https_t, https_data);
  uv_timer_stop(timer);
  curl_multi_socket_action(_this->curl_multi_handle, CURL_SOCKET_TIMEOUT, 0,
                           &_this->running_handles);
  iotjs_https_check_done(https_data);
  printf("Leaving iotjs_https_uv_timeout_callback \n");
}

void iotjs_https_uv_socket_timeout_callback(uv_timer_t* timer) {
  iotjs_https_t* https_data = (iotjs_https_t*)(timer->data);
  IOTJS_VALIDATED_STRUCT_METHOD(iotjs_https_t, https_data);
  double download_bytes = 0;
  double upload_bytes = 0;
  uint64_t total_time_ms = 0;

  if (_this->timeout_ms != -1) {
    curl_easy_getinfo(_this->curl_easy_handle, CURLINFO_SIZE_DOWNLOAD,
                      &download_bytes);
    curl_easy_getinfo(_this->curl_easy_handle, CURLINFO_SIZE_UPLOAD,
                      &upload_bytes);
    total_time_ms = uv_now(_this->loop);
    double total_bytes = download_bytes + upload_bytes;

    printf("----------In on Timeout-------------\n");
    printf("Total bytes Downloaded so far - %f \n", download_bytes);
    printf("Total bytes Uploaded so far - %f \n", upload_bytes);
    printf("Total bytes so far - %f \n", total_bytes);
    printf("last_bytes_num - %f \n", _this->last_bytes_num);
    printf("Time so far - %llu \n", total_time_ms);
    printf("Time for timeout - %llu \n",
           ((uint64_t)_this->timeout_ms + _this->last_bytes_time));

    if (_this->last_bytes_num == total_bytes) {
      printf("Got inside first if \n \n");
      if (total_time_ms >
          ((uint64_t)_this->timeout_ms + _this->last_bytes_time)) {
        if (!_this->request_done) {
          iotjs_https_jcallback(https_data, IOTJS_MAGIC_STRING_ONTIMEOUT,
                                iotjs_jargs_get_empty(), false);
        }
        uv_timer_stop(&(_this->socket_timeout));
      }
    } else {
      _this->last_bytes_num = total_bytes;
      _this->last_bytes_time = total_time_ms;
    }
  }
}

void iotjs_https_set_timeout(long ms, iotjs_https_t* https_data) {
  IOTJS_VALIDATED_STRUCT_METHOD(iotjs_https_t, https_data);
  if (ms < 0)
    return;
  _this->timeout_ms = ms;
  uv_timer_start(&(_this->socket_timeout),
                 iotjs_https_uv_socket_timeout_callback, 1, (uint64_t)ms);

  printf("In iotjs_https_set_timeout \n");
}

void iotjs_https_add_header(iotjs_https_t* https_data,
                            const char* char_header) {
  IOTJS_VALIDATED_STRUCT_METHOD(iotjs_https_t, https_data);
  _this->header_list = curl_slist_append(_this->header_list, char_header);
  if (_this->method == HTTPS_POST || _this->method == HTTPS_PUT) {
    if (strncmp(char_header, "Content-Length: ", strlen("Content-Length: ")) ==
        0) {
      const char* numberString = char_header + strlen("Content-Length: ");
      _this->content_length = strtol(numberString, NULL, 10);
      printf("Got content_length as  %ld\n", _this->content_length);
    }
  }
  printf("In iotjs_https_add_header \n");
}

void iotjs_https_send_request(iotjs_https_t* https_data) {
  IOTJS_VALIDATED_STRUCT_METHOD(iotjs_https_t, https_data);
  // Add all the headers to the easy handle
  curl_easy_setopt(_this->curl_easy_handle, CURLOPT_HTTPHEADER,
                   _this->header_list);

  if (_this->method == HTTPS_POST && _this->content_length != -1)
    curl_easy_setopt(_this->curl_easy_handle, CURLOPT_POSTFIELDSIZE,
                     _this->content_length);
  else if (_this->method == HTTPS_PUT && _this->content_length != -1)
    curl_easy_setopt(_this->curl_easy_handle, CURLOPT_INFILESIZE,
                     _this->content_length);

  curl_multi_add_handle(_this->curl_multi_handle, _this->curl_easy_handle);

  printf("Added download \n");
}

void iotjs_https_js_data_to_write(iotjs_https_t* https_data,
                                  iotjs_string_t read_chunk,
                                  const iotjs_jval_t* callback,
                                  const iotjs_jval_t* onwrite) {
  IOTJS_VALIDATED_STRUCT_METHOD(iotjs_https_t, https_data);

  if (_this->to_destroy_read_onwrite) {
    printf("Destroying read_onwrite \n");
    _this->to_destroy_read_onwrite = false;
    iotjs_string_destroy(&(_this->read_chunk));
    iotjs_jval_destroy(&(_this->read_onwrite));
    iotjs_jval_destroy(&(_this->read_callback));
  }

  _this->read_chunk = read_chunk;
  _this->data_to_read = true;

  _this->read_callback = iotjs_jval_create_copied(callback);
  printf("Got read callback in _write \n");
  _this->read_onwrite = iotjs_jval_create_copied(onwrite);
  _this->to_destroy_read_onwrite = true;
  printf("Got onwrite callback in _write \n");

  if (_this->request_done) {
    iotjs_https_call_read_onwrite_async(https_data);
  } else if (_this->is_stream_writable) {
    printf("Unpaused in _write \n");
    curl_easy_pause(_this->curl_easy_handle, CURLPAUSE_CONT);
    uv_timer_stop(&(_this->timeout));
    uv_timer_start(&(_this->timeout), iotjs_https_uv_timeout_callback, 1, 0);
  }
}

void iotjs_https_finish_request(iotjs_https_t* https_data) {
  IOTJS_VALIDATED_STRUCT_METHOD(iotjs_https_t, https_data);
  _this->stream_ended = true;
  if (_this->request_done) {
    iotjs_https_cleanup(https_data);
  } else if (_this->is_stream_writable) {
    printf("4 \n");
    printf("Unpaused in iotjs_https_finish_request \n");
    curl_easy_pause(_this->curl_easy_handle, CURLPAUSE_CONT);
    uv_timer_stop(&(_this->timeout));
    uv_timer_start(&(_this->timeout), iotjs_https_uv_timeout_callback, 1, 0);
  }
}

void iotjs_https_abort(iotjs_https_t* https_data) {
  iotjs_https_cleanup(https_data);
}

int iotjs_https_curl_start_timeout_callback(CURLM* multi, long timeout_ms,
                                            void* userp) {
  printf("Setting a uv_timer %ld \n", timeout_ms);
  iotjs_https_t* https_data = (iotjs_https_t*)userp;
  IOTJS_VALIDATED_STRUCT_METHOD(iotjs_https_t, https_data);
  if (timeout_ms < 0) {
    uv_timer_stop(&(_this->timeout));
  } else {
    if (timeout_ms == 0)
      timeout_ms =
          1; // 0 means directly call socket_action, but we'll do it in a bit
    if ((_this->timeout_ms != -1) && (timeout_ms > _this->timeout_ms))
      timeout_ms = _this->timeout_ms;
    uv_timer_start(&(_this->timeout), iotjs_https_uv_timeout_callback,
                   (uint64_t)timeout_ms, 0);
  }
  return 0;
}

int iotjs_https_curl_socket_callback(CURL* easy, curl_socket_t sockfd,
                                     int action, void* userp, void* socketp) {
  printf("in iotjs_https_curl_socket_callback \n");
  iotjs_https_t* https_data = (iotjs_https_t*)userp;
  IOTJS_VALIDATED_STRUCT_METHOD(iotjs_https_t, https_data);
  // if(_this->is_stream_writable){
  //    printf("Unpaused in iotjs_https_curl_socket_callback \n");
  //    curl_easy_pause(_this->curl_easy_handle, CURLPAUSE_CONT);
  //}
  if (action == CURL_POLL_IN || action == CURL_POLL_OUT ||
      action == CURL_POLL_INOUT) {
    if (!socketp) {
      _this->sockfd = sockfd;
      printf("Assigned Socket\n");
      uv_poll_init_socket(_this->loop, &_this->poll_handle, sockfd);
      _this->poll_handle.data = (void*)https_data;
      _this->poll_handle_to_be_destroyed = true;
      (&_this->poll_handle)->data = https_data;
      curl_multi_assign(_this->curl_multi_handle, sockfd, (void*)https_data);
    }
  }

  switch (action) {
    case CURL_POLL_IN:
      _this->poll_handle_to_be_destroyed = true;
      uv_poll_start(&_this->poll_handle, UV_READABLE,
                    iotjs_https_uv_poll_callback);
      printf("in in iotjs_https_curl_socket_callback \n");
      break;
    case CURL_POLL_OUT:
      _this->poll_handle_to_be_destroyed = true;
      uv_poll_start(&_this->poll_handle, UV_WRITABLE,
                    iotjs_https_uv_poll_callback);
      printf("in out iotjs_https_curl_socket_callback \n");
      break;
    case CURL_POLL_INOUT:
      _this->poll_handle_to_be_destroyed = true;
      uv_poll_start(&_this->poll_handle, UV_READABLE | UV_WRITABLE,
                    iotjs_https_uv_poll_callback);
      printf("in inout iotjs_https_curl_socket_callback \n");
      break;
    case CURL_POLL_REMOVE:
      printf("in remove iotjs_https_curl_socket_callback \n");
      if (socketp) {
        printf("Leaving iotjs_https_curl_socket_callback POLL_REMOVE \n");
        if (_this->poll_handle_to_be_destroyed) {
          uv_poll_stop(&_this->poll_handle);
          uv_close((uv_handle_t*)&_this->poll_handle, NULL);
          _this->poll_handle_to_be_destroyed = false;
        }
        curl_multi_assign(_this->curl_multi_handle, sockfd, NULL);
      }
      break;
    default:
      abort();
  }

  return 0;
}

void iotjs_https_initialize_curl_opts(iotjs_https_t* https_data) {
  IOTJS_VALIDATED_STRUCT_METHOD(iotjs_https_t, https_data);

  // Setup Some parameters for multi handle
  curl_multi_setopt(_this->curl_multi_handle, CURLMOPT_SOCKETFUNCTION,
                    iotjs_https_curl_socket_callback);
  curl_multi_setopt(_this->curl_multi_handle, CURLMOPT_SOCKETDATA,
                    (void*)https_data);
  curl_multi_setopt(_this->curl_multi_handle, CURLMOPT_TIMERFUNCTION,
                    iotjs_https_curl_start_timeout_callback);
  curl_multi_setopt(_this->curl_multi_handle, CURLMOPT_TIMERDATA,
                    (void*)https_data);

  // TODO: Remove Verbose and Proxy
  curl_easy_setopt(_this->curl_easy_handle, CURLOPT_VERBOSE, 1L);
  curl_easy_setopt(_this->curl_easy_handle, CURLOPT_PROXY, "");

  curl_easy_setopt(_this->curl_easy_handle, CURLOPT_HEADERDATA,
                   (void*)https_data);
  curl_easy_setopt(_this->curl_easy_handle, CURLOPT_WRITEFUNCTION,
                   iotjs_https_curl_write_callback);
  curl_easy_setopt(_this->curl_easy_handle, CURLOPT_WRITEDATA,
                   (void*)https_data);

  // Read and send data to server only for some request types
  if (_this->method == HTTPS_POST || _this->method == HTTPS_PUT ||
      _this->method == HTTPS_CONNECT) {
    curl_easy_setopt(_this->curl_easy_handle, CURLOPT_READFUNCTION,
                     iotjs_https_curl_read_callback);
    curl_easy_setopt(_this->curl_easy_handle, CURLOPT_READDATA,
                     (void*)https_data);
  }

  curl_easy_setopt(_this->curl_easy_handle, CURLOPT_SOCKOPTFUNCTION,
                   iotjs_https_curl_sockopt_callback);
  curl_easy_setopt(_this->curl_easy_handle, CURLOPT_SOCKOPTDATA,
                   (void*)https_data);

  curl_easy_setopt(_this->curl_easy_handle, CURLOPT_URL, _this->URL);
  _this->URL = NULL;
  curl_easy_setopt(_this->curl_easy_handle, CURLOPT_PROTOCOLS,
                   CURLPROTO_HTTP | CURLPROTO_HTTPS);

  if (strlen(_this->ca) > 0)
    curl_easy_setopt(_this->curl_easy_handle, CURLOPT_CAINFO, _this->ca);
  _this->ca = NULL;
  if (strlen(_this->cert) > 0)
    curl_easy_setopt(_this->curl_easy_handle, CURLOPT_SSLCERT, _this->cert);
  _this->cert = NULL;
  if (strlen(_this->key) > 0)
    curl_easy_setopt(_this->curl_easy_handle, CURLOPT_SSLKEY, _this->key);
  _this->key = NULL;

  // Various request types
  switch (_this->method) {
    case HTTPS_GET:
      curl_easy_setopt(_this->curl_easy_handle, CURLOPT_HTTPGET, 1L);
      break;
    case HTTPS_POST:
      curl_easy_setopt(_this->curl_easy_handle, CURLOPT_POST, 1L);
      break;
    case HTTPS_PUT:
      curl_easy_setopt(_this->curl_easy_handle, CURLOPT_UPLOAD, 1L);
      break;
    case HTTPS_DELETE:
      curl_easy_setopt(_this->curl_easy_handle, CURLOPT_CUSTOMREQUEST,
                       "DELETE");
      break;
    case HTTPS_HEAD:
      curl_easy_setopt(_this->curl_easy_handle, CURLOPT_NOBODY, 1L);
      break;
    case HTTPS_CONNECT:
      curl_easy_setopt(_this->curl_easy_handle, CURLOPT_CUSTOMREQUEST,
                       "CONNECT");
      break;
    case HTTPS_OPTIONS:
      curl_easy_setopt(_this->curl_easy_handle, CURLOPT_CUSTOMREQUEST,
                       "OPTIONS");
      break;
    case HTTPS_TRACE:
      curl_easy_setopt(_this->curl_easy_handle, CURLOPT_CUSTOMREQUEST, "TRACE");
      break;
  }

  curl_easy_setopt(_this->curl_easy_handle, CURLOPT_HTTP_TRANSFER_DECODING, 0L);

  printf("Set Most Curl Opts \n");
}

JHANDLER_FUNCTION(createRequest) {
  JHANDLER_CHECK_THIS(object);
  JHANDLER_CHECK_ARGS(1, object);

  const iotjs_jval_t* jthis = JHANDLER_GET_ARG(0, object);

  iotjs_jval_t jhost = iotjs_jval_get_property(jthis, IOTJS_MAGIC_STRING_HOST);
  iotjs_string_t host = iotjs_jval_as_string(&jhost);
  iotjs_jval_destroy(&jhost);
  printf("Got URL in Native Code as %s \n", iotjs_string_data(&host));

  iotjs_jval_t jmethod =
      iotjs_jval_get_property(jthis, IOTJS_MAGIC_STRING_METHOD);
  iotjs_string_t method = iotjs_jval_as_string(&jmethod);
  iotjs_jval_destroy(&jmethod);
  printf("Got method in Native Code as %s \n", iotjs_string_data(&method));

  iotjs_jval_t jca = iotjs_jval_get_property(jthis, IOTJS_MAGIC_STRING_CA);
  iotjs_string_t ca = iotjs_jval_as_string(&jca);
  iotjs_jval_destroy(&jca);
  printf("Got ca in Native Code as %s \n", iotjs_string_data(&ca));

  iotjs_jval_t jcert = iotjs_jval_get_property(jthis, IOTJS_MAGIC_STRING_CERT);
  iotjs_string_t cert = iotjs_jval_as_string(&jcert);
  iotjs_jval_destroy(&jcert);
  printf("Got cert in Native Code as %s \n", iotjs_string_data(&cert));

  iotjs_jval_t jkey = iotjs_jval_get_property(jthis, IOTJS_MAGIC_STRING_KEY);
  iotjs_string_t key = iotjs_jval_as_string(&jkey);
  iotjs_jval_destroy(&jkey);
  printf("Got key in Native Code as %s \n", iotjs_string_data(&key));

  if (curl_global_init(CURL_GLOBAL_SSL)) {
    printf("Could not init cURL\n");
    return;
  }
  iotjs_https_t* https_data =
      iotjs_https_create(iotjs_string_data(&host), iotjs_string_data(&method),
                         iotjs_string_data(&ca), iotjs_string_data(&cert),
                         iotjs_string_data(&key), jthis);

  iotjs_https_initialize_curl_opts(https_data);

  iotjs_string_destroy(&host);
  iotjs_string_destroy(&method);
  iotjs_string_destroy(&ca);
  iotjs_string_destroy(&cert);
  iotjs_string_destroy(&key);
  printf("Leaving createRequest JHANDLER \n");
  iotjs_jhandler_return_null(jhandler);
}

JHANDLER_FUNCTION(addHeader) {
  JHANDLER_CHECK_THIS(object);

  JHANDLER_CHECK_ARGS(2, string, object);
  iotjs_string_t header = JHANDLER_GET_ARG(0, string);
  const char* char_header = iotjs_string_data(&header);
  printf("Got header in Native Code as %s \n", char_header);

  const iotjs_jval_t* jthis = JHANDLER_GET_ARG(1, object);
  iotjs_https_t* https_data =
      (iotjs_https_t*)iotjs_jval_get_object_native_handle(jthis);
  iotjs_https_add_header(https_data, char_header);

  iotjs_string_destroy(&header);
  printf("Leaving addHeader JHANDLER \n");
  iotjs_jhandler_return_null(jhandler);
}

JHANDLER_FUNCTION(sendRequest) {
  JHANDLER_CHECK_THIS(object);

  JHANDLER_CHECK_ARG(0, object);
  const iotjs_jval_t* jthis = JHANDLER_GET_ARG(0, object);
  iotjs_https_t* https_data =
      (iotjs_https_t*)iotjs_jval_get_object_native_handle(jthis);
  iotjs_https_send_request(https_data);

  printf("Leaving sendRequest JHANDLER \n");
  iotjs_jhandler_return_null(jhandler);
}

JHANDLER_FUNCTION(setTimeout) {
  JHANDLER_CHECK_THIS(object);
  JHANDLER_CHECK_ARGS(2, number, object);

  double ms = JHANDLER_GET_ARG(0, number);
  printf("Got timeout time in Native Code as %f \n", ms);
  const iotjs_jval_t* jthis = JHANDLER_GET_ARG(1, object);

  iotjs_https_t* https_data =
      (iotjs_https_t*)iotjs_jval_get_object_native_handle(jthis);
  iotjs_https_set_timeout((long)ms, https_data);

  printf("Leaving setTimeout JHANDLER \n");
  iotjs_jhandler_return_null(jhandler);
}

JHANDLER_FUNCTION(_write) {
  printf("Entered _write JHANDLER \n");
  JHANDLER_CHECK_THIS(object);
  JHANDLER_CHECK_ARGS(2, object, string);
  // Argument 3 can be null, so not checked here, checked below.
  JHANDLER_CHECK_ARG(3, function);

  const iotjs_jval_t* jthis = JHANDLER_GET_ARG(0, object);
  iotjs_string_t read_chunk = JHANDLER_GET_ARG(1, string);
  printf("Got Data in _write as %s \n", iotjs_string_data(&read_chunk));

  const iotjs_jval_t* callback = iotjs_jhandler_get_arg(jhandler, 2);
  const iotjs_jval_t* onwrite = JHANDLER_GET_ARG(3, function);

  iotjs_https_t* https_data =
      (iotjs_https_t*)iotjs_jval_get_object_native_handle(jthis);
  iotjs_https_js_data_to_write(https_data, read_chunk, callback, onwrite);

  // readchunk was copied to https_data, hence not destroyed.
  printf("Leaving _write JHANDLER\n");
  iotjs_jhandler_return_null(jhandler);
}

JHANDLER_FUNCTION(finishRequest) {
  printf("Entered finishRequest JHANDLER\n");
  JHANDLER_CHECK_THIS(object);
  JHANDLER_CHECK_ARG(0, object);

  const iotjs_jval_t* jthis = JHANDLER_GET_ARG(0, object);
  iotjs_https_t* https_data =
      (iotjs_https_t*)iotjs_jval_get_object_native_handle(jthis);
  iotjs_https_finish_request(https_data);

  printf("Leaving finishRequest JHANDLER \n");
  iotjs_jhandler_return_null(jhandler);
}

JHANDLER_FUNCTION(Abort) {
  printf("Entering Abort JHANDLER \n");
  JHANDLER_CHECK_THIS(object);
  JHANDLER_CHECK_ARG(0, object);

  const iotjs_jval_t* jthis = JHANDLER_GET_ARG(0, object);
  iotjs_https_t* https_data =
      (iotjs_https_t*)iotjs_jval_get_object_native_handle(jthis);
  iotjs_https_abort(https_data);

  printf("Leaving Abort JHANDLER \n");
  iotjs_jhandler_return_null(jhandler);
}

iotjs_jval_t InitHttps() {
  iotjs_jval_t https = iotjs_jval_create_object();

  iotjs_jval_set_method(&https, IOTJS_MAGIC_STRING_CREATEREQUEST,
                        createRequest);
  iotjs_jval_set_method(&https, IOTJS_MAGIC_STRING_ADDHEADER, addHeader);
  iotjs_jval_set_method(&https, IOTJS_MAGIC_STRING_SENDREQUEST, sendRequest);
  iotjs_jval_set_method(&https, IOTJS_MAGIC_STRING_SETTIMEOUT, setTimeout);
  iotjs_jval_set_method(&https, IOTJS_MAGIC_STRING__WRITE, _write);
  iotjs_jval_set_method(&https, IOTJS_MAGIC_STRING_FINISHREQUEST,
                        finishRequest);
  iotjs_jval_set_method(&https, IOTJS_MAGIC_STRING_ABORT, Abort);

  return https;
}
