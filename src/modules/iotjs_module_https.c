/* Copyright 2015-present Samsung Electronics Co., Ltd. and other contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *		 http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "iotjs_module_https.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>
#include <inttypes.h>
#include <stdlib.h>

/*
static void https_jthis_native_destroy(void* nativep){
	iotjs_https_t* https_data = (iotjs_https_t*) nativep;
	printf("destroyed native jthis\n");
}

static const jerry_object_native_info_t temp_native_info = { .free_cb = (jerry_object_native_free_callback_t) https_jthis_native_destroy } ;
*/
// Constructor
iotjs_https_t* iotjs_https_create(const char* URL, const char* method, const char* ca, const char* cert, const char* key, const iotjs_jval_t* jthis ){
	iotjs_https_t* https_data = IOTJS_ALLOC(iotjs_https_t);
	IOTJS_VALIDATED_STRUCT_CONSTRUCTOR(iotjs_https_t, https_data);

	// Original Request Details
	printf("Saving URL in _this \n");
	_this->URL=URL;
	_this->header_list = NULL;
	if(strcmp(method,"GET") == 0)
		_this->method=HTTPS_GET;
	else if(strcmp(method,"POST") == 0)
		_this->method=HTTPS_POST;
	else if(strcmp(method,"PUT") == 0)
		_this->method=HTTPS_PUT;
	else if(strcmp(method,"DELETE") == 0)
		_this->method=HTTPS_DELETE;
	else if(strcmp(method,"HEAD") == 0)
		_this->method=HTTPS_HEAD;
	else if(strcmp(method,"CONNECT") == 0)
		_this->method=HTTPS_CONNECT;
	else if(strcmp(method,"OPTIONS") == 0)
		_this->method=HTTPS_OPTIONS;
	else if(strcmp(method,"TRACE") == 0)
		_this->method=HTTPS_TRACE;
	else
		printf("Request method is not valid. Valid options are GET, POST, PUT, DELETE, HEAD"); //TODO: cleanup and gracefully exit.

	//TLS certs stuff
	_this->ca = ca;
	_this->cert = cert;
	_this->key = key;
	//Content Length stuff
	_this->content_length = -1;

	//Handles
	_this->loop = iotjs_environment_loop(iotjs_environment_get());
	_this->jthis_native=iotjs_jval_create_copied(jthis);
	iotjs_jval_set_object_native_handle(& (_this->jthis_native), (uintptr_t) _this, NULL);
	_this->curl_multi_handle = curl_multi_init();
	_this->curl_easy_handle = curl_easy_init();
	_this->timeout.data = (void*) https_data;
	uv_timer_init(_this->loop, &(_this->timeout));
	_this->poll_handle_destroyed = false;

	//Timeout stuff
	_this->timeout_ms=-1;
	_this->last_bytes_num=-1;
	_this->last_bytes_time=0;
	_this->socket_timeout.data = (void*) https_data;
	uv_timer_init(_this->loop, &(_this->socket_timeout));

	//ReadData stuff
	_this->cur_read_index=0;
	_this->is_stream_writable=false;
	_this->stream_ended=false;
	_this->data_to_read=false;
	_this->to_destroy_read_onwrite = false;
	_this->async_read_onwrite.data = (void*) https_data;
	uv_timer_init(_this->loop, &(_this->async_read_onwrite));
	//No Need to read data for following types of requests
	if(_this->method == HTTPS_GET ||
		_this->method == HTTPS_DELETE ||
		_this->method == HTTPS_HEAD ||
		_this->method == HTTPS_OPTIONS ||
		_this->method == HTTPS_TRACE)
			_this->stream_ended=true;


	return https_data;
}

// Destructor
void iotjs_https_destroy(iotjs_https_t* https_data){
	printf("Destroying iotjs_https_t \n");
	IOTJS_VALIDATED_STRUCT_DESTRUCTOR(iotjs_https_t, https_data);

	curl_multi_cleanup(_this->curl_multi_handle);
	uv_close((uv_handle_t*)&(_this->timeout), NULL);
	uv_close((uv_handle_t*)&(_this->socket_timeout), NULL);
	uv_close((uv_handle_t*)&(_this->async_read_onwrite), NULL);

	if(! _this->poll_handle_destroyed){
		uv_poll_stop(&_this->poll_handle);
		uv_close((uv_handle_t *) &_this->poll_handle, NULL);
		_this->poll_handle_destroyed = true;
	}
	curl_slist_free_all(_this->header_list);

	iotjs_jval_destroy(&_this->jthis_native);
	IOTJS_RELEASE(https_data);
	return;
}

iotjs_jval_t* iotjs_https_jthis_from_https(iotjs_https_t* https_data){
	IOTJS_VALIDATED_STRUCT_METHOD(iotjs_https_t, https_data);
	return &(_this->jthis_native);
}

// ------------Actual Functions ----------
// Call any property of ClientRequest._Incoming
void iotjs_https_jcallback(iotjs_https_t* https_data, const char* property, const iotjs_jargs_t* jarg){
	iotjs_jval_t* jthis = iotjs_https_jthis_from_https(https_data);
	if( iotjs_jval_is_null(jthis))
		return;

	iotjs_jval_t jincoming = iotjs_jval_get_property(jthis, IOTJS_MAGIC_STRING__INCOMING);
	iotjs_jval_t cb = iotjs_jval_get_property(&jincoming, property);

	IOTJS_ASSERT(iotjs_jval_is_function(&cb));
	printf("Invoking CallBack To JS %s\n", property);
	iotjs_make_callback(&cb, &jincoming, jarg);

	iotjs_jval_destroy(&jincoming);
	iotjs_jval_destroy(&cb);
}

// Call onWrite and callback after ClientRequest._write
void iotjs_https_call_read_onwrite(uv_timer_t *timer){
	printf("Entered iotjs_https_call_read_onwrite\n");
	iotjs_https_t* https_data = (iotjs_https_t*) (timer->data);
	IOTJS_VALIDATED_STRUCT_METHOD(iotjs_https_t, https_data);

	uv_timer_stop(&(_this->async_read_onwrite));
	if (iotjs_jval_is_null(&_this->jthis_native))
		return;
	const iotjs_jargs_t* jarg = iotjs_jargs_get_empty();
	const iotjs_jval_t* jthis = &(_this->jthis_native);
	IOTJS_ASSERT(iotjs_jval_is_function(&(_this->read_onwrite)));

	printf("Invoking CallBack To JS read_onwrite\n");
	iotjs_make_callback(&(_this->read_onwrite), jthis, jarg);

	if (!iotjs_jval_is_undefined(&(_this->read_callback)))
		iotjs_make_callback(&(_this->read_callback), jthis, jarg);
	printf("Exiting iotjs_https_call_read_onwrite\n");
}

// Call the above method Asynchronously
void iotjs_https_call_read_onwrite_async(iotjs_https_t* https_data){
	IOTJS_VALIDATED_STRUCT_METHOD(iotjs_https_t, https_data);
	uv_timer_start(&(_this->async_read_onwrite), iotjs_https_call_read_onwrite, 0, 0);
	printf("In iotjs_https_call_read_onwrite_async \n");
}


int iotjs_https_curl_sockopt_callback(void *userp, curl_socket_t curlfd, curlsocktype purpose){
	iotjs_https_t* https_data = (iotjs_https_t*) userp;
	//TODO: this if is probably never needed
	//if(purpose == CURLSOCKTYPE_IPCXN)
	iotjs_https_jcallback(https_data, IOTJS_MAGIC_STRING_ONSOCKET, iotjs_jargs_get_empty());
	return CURL_SOCKOPT_OK;
}

size_t iotjs_https_curl_read_callback(void *contents, size_t size, size_t nmemb, void *userp){
	//printf("Entered iotjs_https_curl_read_callback \n");
	iotjs_https_t* https_data = (iotjs_https_t*) userp;
	IOTJS_VALIDATED_STRUCT_METHOD(iotjs_https_t, https_data);
	printf("Entered iotjs_https_curl_read_callback %zu \n", _this->cur_read_index);

	//If stream wasnt made writable yet, make it so.
	if(!_this->is_stream_writable){
		_this->is_stream_writable=true;
		iotjs_https_jcallback(https_data, IOTJS_MAGIC_STRING_ONWRITABLE, iotjs_jargs_get_empty());
		printf("Made Stream Writeable!!! \n");
	}

	if(_this->data_to_read){
		size_t real_size = size * nmemb;
		size_t chunk_size = iotjs_string_size(&(_this->read_chunk));
		size_t left_to_copy_size = chunk_size - _this->cur_read_index;

		if(real_size < 1)
			return 0;

		//send some data
		if(_this->cur_read_index < chunk_size){
			size_t num_to_copy = (left_to_copy_size < real_size) ? left_to_copy_size : real_size;
			printf("in iotjs_https_curl_read_callback %zu  \n", _this->cur_read_index);
			const char* buf = iotjs_string_data(&(_this->read_chunk));
			buf = &buf[_this->cur_read_index];
			strncpy((char *)contents, buf, num_to_copy );
			_this->cur_read_index = _this->cur_read_index+num_to_copy;
			printf("***************** Wrote %zu bytes of data ******************\n", num_to_copy);
			return num_to_copy;
		}

		//Finished sending one chunk of data
		_this->cur_read_index=0;
		_this->data_to_read=false;
		iotjs_string_destroy(&(_this->read_chunk));
		//TODO: call onWrite and callback
		iotjs_https_call_read_onwrite_async(https_data);
	}

	//If the data is sent, and stream hasn't ended, wait for more data
	if(!_this->stream_ended){
		printf("Pausing Read \n");
		return CURL_READFUNC_PAUSE;
	}

	//All done, end the transfer
	printf("Exiting iotjs_https_curl_read_callback Finally\n\n");
	return 0;
}

size_t iotjs_https_curl_write_callback(void *contents, size_t size, size_t nmemb, void *userp){
	iotjs_https_t* https_data = (iotjs_https_t*) userp;
	IOTJS_VALIDATED_STRUCT_METHOD(iotjs_https_t, https_data);
	size_t real_size = size * nmemb;
	printf("Entered iotjs_https_curl_write_callback \n");

	if(!_this->stream_ended || _this->data_to_read){
		printf("Pausing Write \n");
		return CURL_WRITEFUNC_PAUSE;
	}

	if(_this->to_destroy_read_onwrite){
		_this->to_destroy_read_onwrite = false;
		iotjs_jval_destroy(&(_this->read_onwrite));
		iotjs_jval_destroy(&(_this->read_callback));
	}

	//TODO: Separate this out in a different function
	if( iotjs_jval_is_null( &_this->jthis_native ))
		return 0;
	iotjs_jargs_t jarg = iotjs_jargs_create(1);
	iotjs_jval_t jresult_arr = iotjs_jval_create_byte_array(real_size, contents);
	iotjs_string_t jresult_string = iotjs_string_create_with_size(contents, real_size);
	iotjs_jargs_append_string(&jarg, &jresult_string);
	//TODO: Use the jresult_arr Byte Array in production, but in testing use string.
	//iotjs_jargs_append_jval(&jarg, &jresult_arr);

	iotjs_https_jcallback(https_data, IOTJS_MAGIC_STRING_ONBODY, &jarg);

	iotjs_jval_destroy(&jresult_arr);
	iotjs_string_destroy(&jresult_string);
	iotjs_jargs_destroy(&jarg);
	printf("Exiting iotjs_https_curl_write_callback \n\n");

	return real_size;
}


void iotjs_https_check_done(iotjs_https_t* https_data) {
	IOTJS_VALIDATED_STRUCT_METHOD(iotjs_https_t, https_data);
	char *done_url;
	CURLMsg *message;
	int pending;
	bool error = false;

	while ((message = curl_multi_info_read(_this->curl_multi_handle, &pending))) {
		switch (message->msg) {
		case CURLMSG_DONE:
			curl_easy_getinfo(message->easy_handle, CURLINFO_EFFECTIVE_URL,	&done_url);
			printf("%s Request Done \n", done_url);
			break;
		default:
			error=true;
		}
		if(error){
			iotjs_jargs_t jarg = iotjs_jargs_create(1);
			char error[] = "Unknown Error has occured.";
			//TODO: perhaps the exit code should be attached here
			iotjs_string_t jresult_string = iotjs_string_create_with_size(error, strlen(error));
			iotjs_jargs_append_string(&jarg, &jresult_string);
			iotjs_https_jcallback(https_data, IOTJS_MAGIC_STRING_ONERROR, &jarg);
			iotjs_string_destroy(&jresult_string);
			iotjs_jargs_destroy(&jarg);
		}
		//TODO: Check what happens when a request is 404
		uv_timer_stop(&(_this->socket_timeout));
		iotjs_https_jcallback(https_data, IOTJS_MAGIC_STRING_ONEND, iotjs_jargs_get_empty());
		iotjs_https_jcallback(https_data, IOTJS_MAGIC_STRING_ONCLOSED, iotjs_jargs_get_empty());
		curl_multi_remove_handle(_this->curl_multi_handle, message->easy_handle);
		curl_easy_cleanup(message->easy_handle);
		_this->curl_easy_handle = NULL;
		iotjs_https_destroy(https_data);
	}
}

void iotjs_https_uv_poll_callback(uv_poll_t *poll, int status, int events) {
	iotjs_https_t* https_data = (iotjs_https_t*) poll->data ;
	IOTJS_VALIDATED_STRUCT_METHOD(iotjs_https_t, https_data);
	printf("Entered in iotjs_https_uv_poll_callback \n");

	//TODO: Do we need this?
	//uv_timer_stop(&(_this->timeout));
	int running_handles;

	int flags = 0;
	if (status < 0)						flags = CURL_CSELECT_ERR;
	if (!status && events & UV_READABLE) flags |= CURL_CSELECT_IN;
	if (!status && events & UV_WRITABLE) flags |= CURL_CSELECT_OUT;

	curl_multi_socket_action(_this->curl_multi_handle, _this->sockfd, flags, &running_handles);
	iotjs_https_check_done(https_data);
	printf("Leaving iotjs_https_uv_poll_callback Call \n");
}

// This function is for signalling to curl timeout has passed.
// This timeout is usually given by curl itself.
static void iotjs_https_uv_timeout_callback(uv_timer_t *timer) {
	//TODO: do I need to unref/close the timeout handle?
	iotjs_https_t* https_data = (iotjs_https_t*) (timer->data);
	IOTJS_VALIDATED_STRUCT_METHOD(iotjs_https_t, https_data);
	uv_timer_stop(timer);
	int running_handles;
	curl_multi_socket_action(_this->curl_multi_handle, CURL_SOCKET_TIMEOUT, 0, &running_handles);
	iotjs_https_check_done(https_data);
	printf("In on timeout \n");
}

static void iotjs_https_uv_socket_timeout_callback(uv_timer_t *timer){
	//TODO: do I need to unref/close the timeout handle?
	iotjs_https_t* https_data = (iotjs_https_t*) (timer->data);
	IOTJS_VALIDATED_STRUCT_METHOD(iotjs_https_t, https_data);
	double download_bytes = 0;
	double upload_bytes = 0;
	uint64_t total_time_ms = 0;

	if(_this->timeout_ms!=-1){
		curl_easy_getinfo(_this->curl_easy_handle, CURLINFO_SIZE_DOWNLOAD, &download_bytes);
		curl_easy_getinfo(_this->curl_easy_handle, CURLINFO_SIZE_UPLOAD, &upload_bytes);
		total_time_ms = uv_now(_this->loop);
		double total_bytes = download_bytes + upload_bytes;

		printf("----------In on Timeout-------------\n");
		printf("Total bytes Downloaded so far - %f \n", download_bytes);
		printf("Total bytes Uploaded so far - %f \n", upload_bytes);
		printf("Total bytes so far - %f \n", total_bytes);
		printf("last_bytes_num - %f \n", _this->last_bytes_num);
		printf("Time so far - %llu \n", total_time_ms);
		printf("Time for timeout - %llu \n", ((uint64_t)_this->timeout_ms + _this->last_bytes_time));

		if(_this->last_bytes_num == total_bytes){
			printf("Got inside first if \n \n");
			if( total_time_ms > ((uint64_t)_this->timeout_ms + _this->last_bytes_time) ){
				//TODO: Handle the case when request is already over
				iotjs_https_jcallback(https_data, IOTJS_MAGIC_STRING_ONTIMEOUT, iotjs_jargs_get_empty());
				uv_timer_stop(&(_this->socket_timeout));
			}
		}
		else{
			_this->last_bytes_num = total_bytes;
			_this->last_bytes_time = total_time_ms;
		}
	}
}


void iotjs_https_set_timeout(long ms, iotjs_https_t* https_data) {
	IOTJS_VALIDATED_STRUCT_METHOD(iotjs_https_t, https_data);
	//TODO: do I need to unref/close the timeout handle?
	if(ms < 0)
		return;
	_this->timeout_ms = ms;
	//TODO: repeated timeouts
	uv_timer_start(&(_this->socket_timeout), iotjs_https_uv_socket_timeout_callback, 1, (uint64_t) ms);

	printf("In iotjs_https_set_timeout \n");
}


void iotjs_https_add_header(iotjs_https_t* https_data, const char* char_header) {
	IOTJS_VALIDATED_STRUCT_METHOD(iotjs_https_t, https_data);
	_this->header_list = curl_slist_append(_this->header_list, char_header);
	if (_this->method == HTTPS_POST || _this->method == HTTPS_PUT){
		if (strncmp(char_header, "Content-Length: ", strlen("Content-Length: ")) == 0 ){
			const char* numberString = char_header+strlen("Content-Length: ");
			_this->content_length = strtol(numberString, NULL, 10);
			printf("Got content_length as  %ld\n", _this->content_length);
		}
	}
	printf("In iotjs_https_add_header \n");
}


void iotjs_https_send_request(iotjs_https_t* https_data) {
	IOTJS_VALIDATED_STRUCT_METHOD(iotjs_https_t, https_data);
	//Add all the headers to the easy handle
	curl_easy_setopt(_this->curl_easy_handle, CURLOPT_HTTPHEADER, _this->header_list);

	if (_this->method == HTTPS_POST && _this->content_length != -1)
		curl_easy_setopt(_this->curl_easy_handle, CURLOPT_POSTFIELDSIZE, _this->content_length );
	else if (_this->method == HTTPS_PUT && _this->content_length != -1)
		curl_easy_setopt(_this->curl_easy_handle, CURLOPT_INFILESIZE, _this->content_length );

	curl_multi_add_handle(_this->curl_multi_handle, _this->curl_easy_handle);

	printf("Added download \n");
}


void iotjs_https_js_data_to_write(iotjs_https_t* https_data, iotjs_string_t read_chunk, const iotjs_jval_t* callback, const iotjs_jval_t* onwrite){
	IOTJS_VALIDATED_STRUCT_METHOD(iotjs_https_t, https_data);
	_this->read_chunk = read_chunk;
	_this->data_to_read=true;

	if(_this->to_destroy_read_onwrite){
		_this->to_destroy_read_onwrite = false;
		iotjs_jval_destroy(&(_this->read_onwrite));
		iotjs_jval_destroy(&(_this->read_callback));
	}

	_this->read_callback = iotjs_jval_create_copied(callback);
	printf("Got read callback in _write \n");

	_this->read_onwrite = iotjs_jval_create_copied(onwrite);
	_this->to_destroy_read_onwrite = true;
	printf("Got onwrite callback in _write \n");

	if(_this->is_stream_writable){
		printf("Unpaused _write \n");
		curl_easy_pause(_this->curl_easy_handle, CURLPAUSE_CONT);
		uv_timer_stop(&(_this->timeout));
		uv_timer_start(&(_this->timeout), iotjs_https_uv_timeout_callback, 1, 0);
	}
}


void iotjs_https_finish_request(iotjs_https_t* https_data){
	IOTJS_VALIDATED_STRUCT_METHOD(iotjs_https_t, https_data);
 	_this->stream_ended=true;

	if(_this->is_stream_writable){
		printf("4 \n");
		printf("Unpaused in iotjs_https_finish_request \n");
		curl_easy_pause(_this->curl_easy_handle, CURLPAUSE_CONT);
		uv_timer_stop(&(_this->timeout));
		uv_timer_start(&(_this->timeout), iotjs_https_uv_timeout_callback, 1, 0);
	}
}

void iotsj_https_abort(iotjs_https_t* https_data){
	IOTJS_VALIDATED_STRUCT_METHOD(iotjs_https_t, https_data);
	//Should end and close events be fired here?
	uv_timer_stop(&(_this->socket_timeout));
	curl_multi_remove_handle(_this->curl_multi_handle, _this->curl_easy_handle);
	curl_easy_cleanup(_this->curl_easy_handle);
	_this->curl_easy_handle = NULL;
	iotjs_https_destroy(https_data);
}


int iotjs_https_curl_start_timeout_callback(CURLM *multi, long timeout_ms, void *userp) {
	printf("Setting a uv_timer %ld \n", timeout_ms);
	iotjs_https_t* https_data = (iotjs_https_t*) userp;
	IOTJS_VALIDATED_STRUCT_METHOD(iotjs_https_t, https_data);
	if(timeout_ms < 0) {
		uv_timer_stop(&(_this->timeout));
	}
	else {
		if(timeout_ms == 0)
			timeout_ms = 1; //0 means directly call socket_action, but we'll do it in a bit
		if((_this->timeout_ms!=-1)&&(timeout_ms > _this->timeout_ms))
			timeout_ms = _this->timeout_ms;
		uv_timer_start(&(_this->timeout), iotjs_https_uv_timeout_callback,(uint64_t) timeout_ms, 0);
	}
	return 0;
}


int iotjs_https_curl_socket_callback(CURL *easy, curl_socket_t sockfd, int action, void *userp, void *socketp) {
	printf("in iotjs_https_curl_socket_callback \n");
	iotjs_https_t* https_data = (iotjs_https_t*) userp;
	IOTJS_VALIDATED_STRUCT_METHOD(iotjs_https_t, https_data);
	//if(_this->is_stream_writable){
	//	printf("Unpaused in iotjs_https_curl_socket_callback \n");
	//	curl_easy_pause(_this->curl_easy_handle, CURLPAUSE_CONT);
	//}
	if (action == CURL_POLL_IN || action == CURL_POLL_OUT|| action == CURL_POLL_INOUT) {
		if (!socketp) {
			_this->sockfd = sockfd;
			uv_poll_init_socket(_this->loop, &_this->poll_handle, sockfd);
			(&_this->poll_handle)->data = https_data;
			curl_multi_assign(_this->curl_multi_handle, sockfd, (void *) https_data);
		}
	}

	switch (action) {
		case CURL_POLL_IN:
			uv_poll_start(&_this->poll_handle, UV_READABLE, iotjs_https_uv_poll_callback);
			printf("in in iotjs_https_curl_socket_callback \n");
			break;
		case CURL_POLL_OUT:
			uv_poll_start(&_this->poll_handle, UV_WRITABLE, iotjs_https_uv_poll_callback);
			printf("in out iotjs_https_curl_socket_callback \n");
			break;
		case CURL_POLL_INOUT:
			uv_poll_start(&_this->poll_handle, UV_READABLE|UV_WRITABLE, iotjs_https_uv_poll_callback);
			printf("in inout iotjs_https_curl_socket_callback \n");
			break;
		case CURL_POLL_REMOVE:
			printf("in remove iotjs_https_curl_socket_callback \n");
			if (socketp) {
				printf("Leaving iotjs_https_curl_socket_callback POLL_REMOVE \n");
				uv_poll_stop(&_this->poll_handle);
				uv_close((uv_handle_t *) &_this->poll_handle, NULL);
				_this->poll_handle_destroyed = true;
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
	curl_multi_setopt(_this->curl_multi_handle, CURLMOPT_SOCKETFUNCTION, iotjs_https_curl_socket_callback);
	curl_multi_setopt(_this->curl_multi_handle, CURLMOPT_SOCKETDATA, (void*) _this);
	curl_multi_setopt(_this->curl_multi_handle, CURLMOPT_TIMERFUNCTION, iotjs_https_curl_start_timeout_callback);
	curl_multi_setopt(_this->curl_multi_handle, CURLMOPT_TIMERDATA, (void*) _this);

	//TODO: Remove Verbose and Proxy
	curl_easy_setopt(_this->curl_easy_handle, CURLOPT_VERBOSE, 1L);
	curl_easy_setopt(_this->curl_easy_handle, CURLOPT_PROXY, "");

	curl_easy_setopt(_this->curl_easy_handle, CURLOPT_HEADERDATA, (void *) https_data);
	curl_easy_setopt(_this->curl_easy_handle, CURLOPT_WRITEFUNCTION, iotjs_https_curl_write_callback);
	curl_easy_setopt(_this->curl_easy_handle, CURLOPT_WRITEDATA, (void *) https_data);

	//Read and send data to server only for some request types
	if(_this->method == HTTPS_POST ||
		_this->method == HTTPS_PUT ||
		_this->method == HTTPS_CONNECT){
		curl_easy_setopt(_this->curl_easy_handle, CURLOPT_READFUNCTION, iotjs_https_curl_read_callback);
		curl_easy_setopt(_this->curl_easy_handle, CURLOPT_READDATA, (void *) https_data);
	}

	curl_easy_setopt(_this->curl_easy_handle, CURLOPT_SOCKOPTFUNCTION, iotjs_https_curl_sockopt_callback);
	curl_easy_setopt(_this->curl_easy_handle, CURLOPT_SOCKOPTDATA, (void *) https_data);

	curl_easy_setopt(_this->curl_easy_handle, CURLOPT_URL, _this->URL);
	_this->URL = NULL;

	if(strlen(_this->ca) > 0)
		curl_easy_setopt(_this->curl_easy_handle, CURLOPT_CAINFO, _this->ca);
	_this->ca = NULL;
	if(strlen(_this->cert) > 0)
		curl_easy_setopt(_this->curl_easy_handle, CURLOPT_SSLCERT, _this->cert);
	_this->cert = NULL;
	if(strlen(_this->key) > 0)
		curl_easy_setopt(_this->curl_easy_handle, CURLOPT_SSLKEY, _this->key);
	_this->key = NULL;

	//Various request types
	switch(_this->method){
		case HTTPS_GET: curl_easy_setopt(_this->curl_easy_handle, CURLOPT_HTTPGET, 1L); break;
		case HTTPS_POST: curl_easy_setopt(_this->curl_easy_handle, CURLOPT_POST, 1L); break;
		case HTTPS_PUT: curl_easy_setopt(_this->curl_easy_handle, CURLOPT_UPLOAD, 1L); break;
		case HTTPS_DELETE: curl_easy_setopt(_this->curl_easy_handle, CURLOPT_CUSTOMREQUEST, "DELETE"); break;
		case HTTPS_HEAD: curl_easy_setopt(_this->curl_easy_handle, CURLOPT_NOBODY, 1L); break;
		case HTTPS_CONNECT: curl_easy_setopt(_this->curl_easy_handle, CURLOPT_CUSTOMREQUEST, "CONNECT"); break;
		case HTTPS_OPTIONS: curl_easy_setopt(_this->curl_easy_handle, CURLOPT_CUSTOMREQUEST, "OPTIONS"); break;
		case HTTPS_TRACE: curl_easy_setopt(_this->curl_easy_handle, CURLOPT_CUSTOMREQUEST, "TRACE"); break;
	}

	//TODO: Proxy support is applicable from libcurl version 7.54.0. Current headless version is 7.53.1
	//curl_easy_setopt(handle, CURLOPT_PROXY, "http://10.112.1.184:8080/");
	//curl_easy_setopt(handle, CURLOPT_SUPPRESS_CONNECT_HEADERS, 1L);
	//curl_easy_setopt(_this->curl_easy_handle, CURLOPT_HTTP_TRANSFER_DECODING, 0L);

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

	iotjs_jval_t jmethod = iotjs_jval_get_property(jthis, IOTJS_MAGIC_STRING_METHOD);
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
	iotjs_https_t* https_data = iotjs_https_create(iotjs_string_data(&host),
		iotjs_string_data(&method), iotjs_string_data(&ca), iotjs_string_data(&cert),
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
	iotjs_https_t* https_data = (iotjs_https_t*) iotjs_jval_get_object_native_handle(jthis);
	iotjs_https_add_header (https_data, char_header);

	iotjs_string_destroy(&header);
	printf("Leaving addHeader JHANDLER \n");
	iotjs_jhandler_return_null(jhandler);
}

JHANDLER_FUNCTION(sendRequest) {
	JHANDLER_CHECK_THIS(object);

	JHANDLER_CHECK_ARG(0, object);
	const iotjs_jval_t* jthis = JHANDLER_GET_ARG(0, object);
	iotjs_https_t* https_data = (iotjs_https_t*) iotjs_jval_get_object_native_handle(jthis);
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

	iotjs_https_t* https_data = (iotjs_https_t*) iotjs_jval_get_object_native_handle(jthis);
	iotjs_https_set_timeout( (long) ms, https_data);

	printf("Leaving setTimeout JHANDLER \n");
	iotjs_jhandler_return_null(jhandler);
}

JHANDLER_FUNCTION(_write) {
	printf("Entered _write JHANDLER \n");
	JHANDLER_CHECK_THIS(object);
	JHANDLER_CHECK_ARGS(2, object, string);
	//Argument 3 can be null, so not checked here, checked below.
	JHANDLER_CHECK_ARG(3, function);

	const iotjs_jval_t* jthis = JHANDLER_GET_ARG(0, object);
	iotjs_string_t read_chunk = JHANDLER_GET_ARG(1, string);
	printf("Got Data in _write as %s \n", iotjs_string_data(&read_chunk));

	const iotjs_jval_t* callback = iotjs_jhandler_get_arg(jhandler, 2);
	const iotjs_jval_t* onwrite = JHANDLER_GET_ARG(3, function);

	iotjs_https_t* https_data = (iotjs_https_t*) iotjs_jval_get_object_native_handle(jthis);
	iotjs_https_js_data_to_write(https_data, read_chunk, callback, onwrite);

	//readchunk was copied to https_data, hence not destroyed.
	printf("Leaving _write JHANDLER\n");
	iotjs_jhandler_return_null(jhandler);
}

JHANDLER_FUNCTION(finishRequest) {
	printf("Entered finishRequest JHANDLER\n");
	JHANDLER_CHECK_THIS(object);
	JHANDLER_CHECK_ARG(0, object);

	const iotjs_jval_t* jthis = JHANDLER_GET_ARG(0, object);
	iotjs_https_t* https_data = (iotjs_https_t*) iotjs_jval_get_object_native_handle(jthis);
	iotjs_https_finish_request(https_data);

	printf("Leaving finishRequest JHANDLER \n");
	iotjs_jhandler_return_null(jhandler);
}

JHANDLER_FUNCTION(Abort) {
	printf("Entering Abort");
	JHANDLER_CHECK_THIS(object);
	JHANDLER_CHECK_ARG(0, object);

	const iotjs_jval_t* jthis = JHANDLER_GET_ARG(0, object);
	iotjs_https_t* https_data = (iotjs_https_t*) iotjs_jval_get_object_native_handle(jthis);
	iotsj_https_abort(https_data);

	printf("Leaving Abort \n");
	iotjs_jhandler_return_null(jhandler);
}

iotjs_jval_t InitHttps() {
	iotjs_jval_t https = iotjs_jval_create_object();

	iotjs_jval_set_method(&https, IOTJS_MAGIC_STRING_CREATEREQUEST, createRequest);
	iotjs_jval_set_method(&https, IOTJS_MAGIC_STRING_ADDHEADER, addHeader);
	iotjs_jval_set_method(&https, IOTJS_MAGIC_STRING_SENDREQUEST, sendRequest);
	iotjs_jval_set_method(&https, IOTJS_MAGIC_STRING_SETTIMEOUT, setTimeout);
	iotjs_jval_set_method(&https, IOTJS_MAGIC_STRING__WRITE, _write);
	iotjs_jval_set_method(&https, IOTJS_MAGIC_STRING_FINISHREQUEST, finishRequest);
	iotjs_jval_set_method(&https, IOTJS_MAGIC_STRING_ABORT, Abort);

	return https;
}
