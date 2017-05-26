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
#include "iotjs_def.h"
#include <curl/curl.h>
#include <uv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>
#include <inttypes.h>
#include <stdlib.h>


// -----------Some Global Structs-----------
struct GlobalData;
static void set_timeout(long ms, struct GlobalData* globalData);

typedef struct curl_context_t {
	uv_poll_t poll_handle;
	curl_socket_t sockfd;
	struct GlobalData* globalData;
} curl_context_t;

typedef struct read_data_t{
	iotjs_jval_t chunk;
	iotjs_jval_t callback;
	iotjs_jval_t onwrite;
	struct GlobalData* globalData;
} read_data_t;

typedef struct GlobalData {
	uv_timer_t timeout;
	iotjs_jval_t jthis_native;
	uv_loop_t *loop;
	CURLM *curl_handle;
	CURL *curl_easy_handle;
	curl_context_t* context;
	const char* URL;
	HTTPS_Methods method;
	struct curl_slist *headerList;

	bool headersDone;

	//For SetTimeOut
	uv_timer_t socket_timeout;
	long timeout_ms;
	double lastNumBytes;
	uint64_t lastTime;

	//For ReadData
	bool isStreamWritable;
	bool dataToRead;
	bool streamEnded;
	iotjs_string_t readChunk;
	iotjs_jval_t readCallback;
	uv_timer_t async_readOnWrite;
	iotjs_jval_t readOnWrite;
	bool toDestroyReadOnWrite;
	size_t curReadIndex;

	//Content-Length for Post and Put
	long contentLength;

	//TLS certs Options
	const char* ca;
	const char* cert;
	const char* key;

} GlobalData;

static void https_tempjval_destroy(void* nativep){
 GlobalData* globalData = (GlobalData*) nativep;
 printf("Address contained in the pointer is - %p", globalData);
 printf("FREEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEE \n");
}


static const jerry_object_native_info_t temp_native_info = { .free_cb = (jerry_object_native_free_callback_t) https_tempjval_destroy } ;

// ------------Curl Context------------
static curl_context_t* create_curl_context(curl_socket_t sockfd, GlobalData* globalData)
{
	curl_context_t *context;
	context = (curl_context_t *) malloc(sizeof *context);
	context->sockfd = sockfd;
	context->globalData = globalData;

	uv_poll_init_socket(globalData->loop, &context->poll_handle, sockfd);
	context->poll_handle.data = context;

	return context;
}

static void curl_close_cb(uv_handle_t *handle)
{
	curl_context_t *context = (curl_context_t *) handle->data;
	free(context);
}

static void destroy_curl_context(curl_context_t *context)
{
	uv_close((uv_handle_t *) &context->poll_handle, curl_close_cb);
}

void destroy_GlobalData(GlobalData* globalData){
	printf("Destroying GlobalData \n");

	curl_multi_cleanup(globalData->curl_handle);
	uv_close((uv_handle_t*)&(globalData->timeout), NULL);
	uv_close((uv_handle_t*)&(globalData->socket_timeout), NULL);
	uv_close((uv_handle_t*)&(globalData->async_readOnWrite), NULL);
	printf("Leaving check_multi_info Call 2 \n");

	if(globalData->context != NULL){
		destroy_curl_context(globalData->context);
		globalData->context = NULL;
	}
	curl_slist_free_all(globalData->headerList);

	iotjs_jval_destroy (&globalData->jthis_native);
	//globalData->jthis_native = &iotjs_jval_get_null();

	free( globalData);
	return;
}

// ------------Actual Functions ----------

static void javascriptCallback(GlobalData* globalData, const char* property){
	if( iotjs_jval_is_null( &globalData->jthis_native ))
		return;

	const iotjs_jargs_t* jarg = iotjs_jargs_get_empty();
	const iotjs_jval_t* jobject = &(globalData->jthis_native);
	iotjs_jval_t jobject1 =	iotjs_jval_get_property(jobject, IOTJS_MAGIC_STRING__INCOMING);
	iotjs_jval_t cb = iotjs_jval_get_property(&jobject1, property);

	IOTJS_ASSERT(iotjs_jval_is_function(&cb));
	printf("Invoking CallBack To JS %s\n", property);
	iotjs_make_callback(&cb, &jobject1, jarg);

	iotjs_jval_destroy(&jobject1);
	iotjs_jval_destroy(&cb);
}

static void callReadOnWrite(uv_timer_t *timer){
	GlobalData* globalData = (GlobalData*) (timer->data);
	uv_timer_stop(&(globalData->async_readOnWrite));
	printf("Entered callReadOnWrite\n");
	if( iotjs_jval_is_null( &globalData->jthis_native ))
		return;
			const iotjs_jargs_t* jarg = iotjs_jargs_get_empty();
			const iotjs_jval_t* jobject = &(globalData->jthis_native);

			if(iotjs_jval_is_undefined(&(globalData->readOnWrite)))
				printf("Is undefined \n");

			if(iotjs_jval_is_null(&(globalData->readOnWrite)))
				printf("Is null \n");

			IOTJS_ASSERT(iotjs_jval_is_function(&(globalData->readOnWrite)));

			printf("Invoking CallBack To JS callReadOnWrite\n");
			//iotjs_jhelper_call_ok(&(globalData->readOnWrite), jobject, jarg);
			iotjs_make_callback(&(globalData->readOnWrite), jobject, jarg);
			if(!iotjs_jval_is_undefined(&(globalData->readCallback)))
				iotjs_make_callback(&(globalData->readCallback), jobject, jarg);
			//if(globalData->toDestroyReadOnWrite){
				//iotjs_jval_destroy(&(globalData->readOnWrite));
				//globalData->toDestroyReadOnWrite = false;
			//}
	printf("Exiting callReadOnWrite\n");
}

static void async_callReadOnWrite(GlobalData* globalData){
	uv_timer_start(&(globalData->async_readOnWrite), callReadOnWrite, 0, 0);
	printf("In async_callReadOnWrite \n");
}


static int socketCallback(void *userp, curl_socket_t curlfd, curlsocktype purpose){
	GlobalData* globalData = (GlobalData*) userp;
	if(purpose == CURLSOCKTYPE_IPCXN)
		javascriptCallback(globalData, IOTJS_MAGIC_STRING_ONSOCKET);
	return CURL_SOCKOPT_OK;
}

static size_t
ReadBodyCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
	//printf("Entered ReadBodyCallback \n");
	GlobalData* globalData = (GlobalData*) userp;
	printf("Entered ReadBodyCallback %zu \n", globalData->curReadIndex);

	//If stream wasnt made writable yet, make it so.
	if(!globalData->isStreamWritable){
		globalData->isStreamWritable=true;
		javascriptCallback(globalData, IOTJS_MAGIC_STRING_ONWRITABLE);
		printf("Made Stream Writeable!!! \n");
	}

	if(globalData->dataToRead){
		size_t realsize = size * nmemb;
		size_t chunkSize = iotjs_string_size(&(globalData->readChunk));
		size_t leftToCopySize = chunkSize - globalData->curReadIndex;

		if(realsize < 1)
			return 0;

		//send some data
		if(globalData->curReadIndex < chunkSize){
			size_t numToCopy = (leftToCopySize < realsize) ? leftToCopySize : realsize;
			printf("in ReadBodyCallback %zu  \n", globalData->curReadIndex);
			const char* buf = iotjs_string_data(&(globalData->readChunk));
			buf = &buf[globalData->curReadIndex];
			strncpy((char *)contents, buf, numToCopy );
			globalData->curReadIndex = globalData->curReadIndex+numToCopy;
			printf("***************** Wrote %zu bytes of data ******************\n", numToCopy);
			return numToCopy;
		}

		//Finished sending one chunk of data
		globalData->curReadIndex=0;
		globalData->dataToRead=false;
		iotjs_string_destroy(&(globalData->readChunk));
		//TODO: call onWrite and callback
		async_callReadOnWrite(globalData);

	}

	//If the data is sent, and stream hasn't ended, wait for more data
	if(!globalData->streamEnded){
		printf("Pausing Read \n");
		return CURL_READFUNC_PAUSE;
	}

	//All done, end the transfer
	printf("Exiting ReadBodyCallback Finally\n\n");
	return 0;
}

static size_t
WriteBodyCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
	GlobalData* globalData = (GlobalData*) userp;
	size_t realsize = size * nmemb;
	printf("Entered WriteMemoryCallback \n");

	if(!globalData->streamEnded || globalData->dataToRead){
		printf("Pausing Write \n");
		//
		return CURL_WRITEFUNC_PAUSE;
	}
	if(globalData->toDestroyReadOnWrite){
		globalData->toDestroyReadOnWrite = false;
		iotjs_jval_destroy(&(globalData->readOnWrite));
		iotjs_jval_destroy(&(globalData->readCallback));
	}

	if(!globalData->headersDone){
		globalData->headersDone = true;
		//callResponse(globalData);
	}

	//TODO: Separate this out in a different function
	if( iotjs_jval_is_null( &globalData->jthis_native ))
		return 0;
			iotjs_jargs_t jarg = iotjs_jargs_create(1);
			iotjs_jval_t jResultArr = iotjs_jval_create_byte_array(realsize, contents);
			iotjs_string_t jResultString = iotjs_string_create_with_size(contents, realsize);
			iotjs_jargs_append_string(&jarg, &jResultString);
			//TODO: Use the jResultArr Byte Array in production, but in testing use string.
			//iotjs_jargs_append_jval(&jarg, &jResultArr);

			const iotjs_jval_t* jobject = &(globalData->jthis_native);
			iotjs_jval_t jobject1 =	iotjs_jval_get_property(jobject, "_incoming");
			iotjs_jval_t cb = iotjs_jval_get_property(&jobject1, "OnBody");

			IOTJS_ASSERT(iotjs_jval_is_function(&cb));

			printf("Invoking CallBack To JS WriteBodyCallback\n");
			iotjs_make_callback(&cb, &jobject1, &jarg);

			iotjs_jval_destroy(&jobject1);
			iotjs_jval_destroy(&cb);
			iotjs_jval_destroy(&jResultArr);
			iotjs_string_destroy(&jResultString);
			iotjs_jargs_destroy(&jarg);
	printf("Exiting WriteMemoryCallback \n\n");

	return realsize;
}

void add_download(GlobalData* globalData) {

	globalData->curl_easy_handle = curl_easy_init();

	/* send all data to this function	*/
	//curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, WriteHeaderCallback);
	curl_easy_setopt(globalData->curl_easy_handle, CURLOPT_VERBOSE, 1L);
	curl_easy_setopt(globalData->curl_easy_handle, CURLOPT_PROXY, "");

	curl_easy_setopt(globalData->curl_easy_handle, CURLOPT_HEADERDATA, (void *)globalData);
	curl_easy_setopt(globalData->curl_easy_handle, CURLOPT_WRITEFUNCTION, WriteBodyCallback);
	curl_easy_setopt(globalData->curl_easy_handle, CURLOPT_WRITEDATA, (void *)globalData);

	//Read and send data to server only for some request types
	if(globalData->method == HTTPS_POST ||
		globalData->method == HTTPS_PUT ||
		globalData->method == HTTPS_CONNECT){
		curl_easy_setopt(globalData->curl_easy_handle, CURLOPT_READFUNCTION, ReadBodyCallback);
		curl_easy_setopt(globalData->curl_easy_handle, CURLOPT_READDATA, (void *)globalData);
	}

	curl_easy_setopt(globalData->curl_easy_handle, CURLOPT_SOCKOPTFUNCTION, socketCallback);
	curl_easy_setopt(globalData->curl_easy_handle, CURLOPT_SOCKOPTDATA, (void *)globalData);

	curl_easy_setopt(globalData->curl_easy_handle, CURLOPT_URL, globalData->URL);

	if(strlen(globalData->ca) > 0)
		curl_easy_setopt(globalData->curl_easy_handle, CURLOPT_CAINFO, globalData->ca);
	if(strlen(globalData->cert) > 0)
		curl_easy_setopt(globalData->curl_easy_handle, CURLOPT_SSLCERT, globalData->cert);
	if(strlen(globalData->key) > 0)
		curl_easy_setopt(globalData->curl_easy_handle, CURLOPT_SSLKEY, globalData->key);

	//Various request types
	if(globalData->method == HTTPS_GET)
		curl_easy_setopt(globalData->curl_easy_handle, CURLOPT_HTTPGET, 1L);
	else if(globalData->method == HTTPS_POST)
		curl_easy_setopt(globalData->curl_easy_handle, CURLOPT_POST, 1L);
	else if(globalData->method == HTTPS_PUT)
		curl_easy_setopt(globalData->curl_easy_handle, CURLOPT_UPLOAD, 1L);
	else if(globalData->method == HTTPS_DELETE)
		curl_easy_setopt(globalData->curl_easy_handle, CURLOPT_CUSTOMREQUEST, "DELETE");
	else if(globalData->method == HTTPS_HEAD)
		curl_easy_setopt(globalData->curl_easy_handle, CURLOPT_NOBODY, 1L);
	else if(globalData->method == HTTPS_CONNECT)
		curl_easy_setopt(globalData->curl_easy_handle, CURLOPT_CUSTOMREQUEST, "CONNECT");
	else if(globalData->method == HTTPS_OPTIONS)
		curl_easy_setopt(globalData->curl_easy_handle, CURLOPT_CUSTOMREQUEST, "OPTIONS");
	else if(globalData->method == HTTPS_TRACE)
		curl_easy_setopt(globalData->curl_easy_handle, CURLOPT_CUSTOMREQUEST, "TRACE");

	//TODO: Proxy support is applicable from libcurl version 7.54.0. Current headless version is 7.53.1
	//curl_easy_setopt(handle, CURLOPT_PROXY, "http://10.112.1.184:8080/");
	//curl_easy_setopt(handle, CURLOPT_SUPPRESS_CONNECT_HEADERS, 1L);
	//curl_easy_setopt(globalData->curl_easy_handle, CURLOPT_HTTP_TRANSFER_DECODING, 0L);

	fprintf(stdout, "Created Curl Easy Stuff \n");
}

void check_multi_info(GlobalData* globalData) {
	char *done_url;
	CURLMsg *message;
	int pending;

	while ((message = curl_multi_info_read(globalData->curl_handle, &pending))) {
		switch (message->msg) {
		case CURLMSG_DONE:
			curl_easy_getinfo(message->easy_handle, CURLINFO_EFFECTIVE_URL,
							&done_url);
			printf("%s DONE\n", done_url);
			uv_timer_stop(&(globalData->socket_timeout));
			javascriptCallback(globalData, IOTJS_MAGIC_STRING_ONEND);
			javascriptCallback(globalData, IOTJS_MAGIC_STRING_ONCLOSED);
			curl_multi_remove_handle(globalData->curl_handle, message->easy_handle);
			curl_easy_cleanup(message->easy_handle);
			globalData->curl_easy_handle = NULL;
			destroy_GlobalData(globalData);
			break;

		default:
			fprintf(stderr, "CURLMSG default\n");
			abort();
		}
	}
}

void curl_perform(uv_poll_t *poll, int status, int events) {
	curl_context_t *context = (curl_context_t*) poll->data ;
	GlobalData* globalData = context->globalData;

	printf("Entered in curl_perform \n");

	//TODO: Do we need this?
	//uv_timer_stop(&(globalData->timeout));
	int running_handles;

	int flags = 0;
	if (status < 0)						flags = CURL_CSELECT_ERR;
	if (!status && events & UV_READABLE) flags |= CURL_CSELECT_IN;
	if (!status && events & UV_WRITABLE) flags |= CURL_CSELECT_OUT;

	curl_multi_socket_action(globalData->curl_handle, context->sockfd, flags, &running_handles);
	check_multi_info(globalData);
	printf("Leaving curl_perform Call \n");
}

static void on_timeout(uv_timer_t *timer) {
//TODO: do I need to unref/close the timeout handle?
	GlobalData* globalData = (GlobalData*) (timer->data);
	uv_timer_stop(timer);
	int running_handles;
	curl_multi_socket_action(globalData->curl_handle, CURL_SOCKET_TIMEOUT, 0, &running_handles);
	check_multi_info(globalData);
	printf("In on timeout \n");
}
static void socket_timeout(uv_timer_t *timer){
	//TODO: do I need to unref/close the timeout handle?
	GlobalData* globalData = (GlobalData*) (timer->data);
	double downloadBytes = 0;
	double uploadBytes = 0;
	uint64_t totalTime_ms = 0;

	if(globalData->timeout_ms!=-1){
		curl_easy_getinfo(globalData->curl_easy_handle, CURLINFO_SIZE_DOWNLOAD, &downloadBytes);
		curl_easy_getinfo(globalData->curl_easy_handle, CURLINFO_SIZE_UPLOAD, &uploadBytes);
		totalTime_ms = uv_now(globalData->loop);
		double totalBytes = downloadBytes + uploadBytes;

		printf("----------In on Timeout-------------\n");
		printf("Total bytes Downloaded so far - %f \n", downloadBytes);
		printf("Total bytes Uploaded so far - %f \n", uploadBytes);
		printf("Total bytes so far - %f \n", totalBytes);
		printf("LastNumBytes - %f \n", globalData->lastNumBytes);
		printf("Time so far - %llu \n", totalTime_ms);
		printf("Time for timeout - %llu \n", ((uint64_t)globalData->timeout_ms + globalData->lastTime));

		if(globalData->lastNumBytes == totalBytes){
			printf("Got inside first if \n \n");
			if( totalTime_ms > ((uint64_t)globalData->timeout_ms + globalData->lastTime) ){
				//TODO: Handle the case when request is already over
				javascriptCallback(globalData, IOTJS_MAGIC_STRING_ONTIMEOUT);
				uv_timer_stop(&(globalData->socket_timeout));
			}
		}
		else{
			globalData->lastNumBytes = totalBytes;
			globalData->lastTime = totalTime_ms;
		}
	}
}

static void set_timeout(long ms, GlobalData* globalData) {
//TODO: do I need to unref/close the timeout handle?
	if(ms < 0)
		return;
	globalData->timeout_ms = ms;
	//TODO: repeated timeouts
	uv_timer_start(&(globalData->socket_timeout), socket_timeout, 1, (uint64_t) ms);

	printf("In set_timeout \n");
}

static int start_timeout(CURLM *multi, long timeout_ms, void *userp) {
printf("Setting a uv_timer %ld \n", timeout_ms);

	GlobalData* globalData = (GlobalData*) userp;
	if(timeout_ms < 0) {
		uv_timer_stop(&(globalData->timeout));
	}
	else {
		if(timeout_ms == 0)
			timeout_ms = 1; //0 means directly call socket_action, but we'll do it in a bit
		if((globalData->timeout_ms!=-1)&&(timeout_ms > globalData->timeout_ms))
			timeout_ms = globalData->timeout_ms;
		uv_timer_start(&(globalData->timeout), on_timeout,(uint64_t) timeout_ms, 0);
	}
	return 0;
}


int handle_socket(CURL *easy, curl_socket_t s, int action, void *userp, void *socketp) {
	printf("in HandleSocket \n");
	GlobalData* globalData = (GlobalData*) userp;
	//if(globalData->isStreamWritable){
	//	printf("Unpaused in handle_socket \n");
	//	curl_easy_pause(globalData->curl_easy_handle, CURLPAUSE_CONT);
	//}
	curl_context_t *curl_context = NULL;
	if (action == CURL_POLL_IN || action == CURL_POLL_OUT|| action == CURL_POLL_INOUT) {
		if (socketp) {
			curl_context = (curl_context_t*) socketp;
		}
		else {
			curl_context = create_curl_context(s, globalData);
			curl_multi_assign(globalData->curl_handle, s, (void *) curl_context);
		}
	}

	switch (action) {
		case CURL_POLL_IN:
			uv_poll_start(&curl_context->poll_handle, UV_READABLE, curl_perform);
			printf("in in HandleSocket \n");
			break;
		case CURL_POLL_OUT:
			uv_poll_start(&curl_context->poll_handle, UV_WRITABLE, curl_perform);
			printf("in out HandleSocket \n");
			break;
		case CURL_POLL_INOUT:
			uv_poll_start(&curl_context->poll_handle, UV_READABLE|UV_WRITABLE, curl_perform);
			printf("in inout HandleSocket \n");
			break;
		case CURL_POLL_REMOVE:
			printf("in remove HandleSocket \n");
			if (socketp) {
				printf("Leaving handle_socket POLL_REMOVE \n");
				uv_poll_stop(&((curl_context_t*)socketp)->poll_handle);
				destroy_curl_context((curl_context_t*) socketp);
				globalData->context = NULL;
				curl_multi_assign(globalData->curl_handle, s, NULL);
			}
			break;
		default:
			abort();
	}

	return 0;
}

void doAll(const char* URL,	const char* method, const char* ca, const char* cert, const char* key, const iotjs_jval_t* jthis){
	//loop = malloc(sizeof(uv_loop_t));
		//uv_loop_init(loop);

	if (curl_global_init(CURL_GLOBAL_SSL)) {
		fprintf(stderr, "Could not init cURL\n");
		return;
	}

	GlobalData* globalData = (GlobalData*)malloc(sizeof(GlobalData));
	printf("The Address of globalData is %p \n", globalData);
	if(NULL == globalData)	{
		printf("\n No more memory to great structures. Failure. \n");
		return;
	}

	globalData->loop = iotjs_environment_loop(iotjs_environment_get());
	globalData->jthis_native=iotjs_jval_create_copied(jthis);
	iotjs_jval_set_object_native_handle(& (globalData->jthis_native), (uintptr_t) globalData, &temp_native_info);
	globalData->headerList = NULL;

	globalData->curl_handle = curl_multi_init();
	globalData->timeout.data = (void*) globalData;
	uv_timer_init(globalData->loop, &(globalData->timeout));

	printf("Saving URL in globalData \n");
	//TODO: copy URL if need be. It'll be destroyed.
	globalData->URL=URL;

	if(strcmp(method,"GET") == 0)
		globalData->method=HTTPS_GET;
	else if(strcmp(method,"POST") == 0)
		globalData->method=HTTPS_POST;
	else if(strcmp(method,"PUT") == 0)
		globalData->method=HTTPS_PUT;
	else if(strcmp(method,"DELETE") == 0)
		globalData->method=HTTPS_DELETE;
	else if(strcmp(method,"HEAD") == 0)
		globalData->method=HTTPS_HEAD;
	else if(strcmp(method,"CONNECT") == 0)
		globalData->method=HTTPS_CONNECT;
	else if(strcmp(method,"OPTIONS") == 0)
		globalData->method=HTTPS_OPTIONS;
	else if(strcmp(method,"TRACE") == 0)
		globalData->method=HTTPS_TRACE;
	else{
		printf("Request method is not valid. Valid options are GET, POST, PUT, DELETE, HEAD");
		//TODO: cleanup and gracefully exit.
	}

	//Timeout stuff
	globalData->timeout_ms=-1;
	globalData->lastNumBytes=-1;
	globalData->socket_timeout.data = (void*) globalData;
	uv_timer_init(globalData->loop, &(globalData->socket_timeout));

	//ReadData stuff
	globalData->curReadIndex=0;
	globalData->isStreamWritable=false;
	globalData->streamEnded=false;
	globalData->dataToRead=false;
	globalData->toDestroyReadOnWrite = false;
	globalData->async_readOnWrite.data = (void*) globalData;
	uv_timer_init(globalData->loop, &(globalData->async_readOnWrite));
	//No Need to read data for following types of requests
	if(globalData->method == HTTPS_GET ||
		globalData->method == HTTPS_DELETE ||
		globalData->method == HTTPS_HEAD ||
		globalData->method == HTTPS_OPTIONS ||
		globalData->method == HTTPS_TRACE){
		globalData->streamEnded=true;
	}

	curl_multi_setopt(globalData->curl_handle, CURLMOPT_SOCKETFUNCTION, handle_socket);
	curl_multi_setopt(globalData->curl_handle, CURLMOPT_SOCKETDATA, (void*) globalData);
	curl_multi_setopt(globalData->curl_handle, CURLMOPT_TIMERFUNCTION, start_timeout);
	curl_multi_setopt(globalData->curl_handle, CURLMOPT_TIMERDATA, (void*) globalData);

	//Content Length stuff
	globalData->contentLength = -1;

	//TLS certs stuff
	globalData->ca = ca;
	globalData->cert = cert;
	globalData->key = key;

	add_download(globalData);
	//curl_multi_cleanup(curl_handle);
	return;
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

	doAll(iotjs_string_data(&host), iotjs_string_data(&method), iotjs_string_data(&ca), iotjs_string_data(&cert), iotjs_string_data(&key), jthis);

	iotjs_string_destroy(&host);
	iotjs_string_destroy(&method);
	iotjs_string_destroy(&ca);
	iotjs_string_destroy(&cert);
	iotjs_string_destroy(&key);
	printf("Leaving JHANDLER \n");
	iotjs_jhandler_return_null(jhandler);
}

JHANDLER_FUNCTION(addHeader) {
	JHANDLER_CHECK_THIS(object);

	JHANDLER_CHECK_ARGS(2, string, object);
	iotjs_string_t header = JHANDLER_GET_ARG(0, string);
	const char* charHeader = iotjs_string_data(&header);
	printf("Got header in Native Code as %s \n", charHeader);

	const iotjs_jval_t* jthis = JHANDLER_GET_ARG(1, object);

	GlobalData* globalData = (GlobalData*) iotjs_jval_get_object_native_handle(jthis);

	globalData->headerList = curl_slist_append(globalData->headerList, charHeader);
	if (globalData->method == HTTPS_POST || globalData->method == HTTPS_PUT){
		if (strncmp(charHeader, "Content-Length: ", strlen("Content-Length: ")) == 0 ){
			const char* numberString = charHeader+strlen("Content-Length: ");
			globalData->contentLength = strtol(numberString, NULL, 10);
			printf("Got contentLength as  %ld\n", globalData->contentLength);
		}
	}

	iotjs_string_destroy(&header);
	printf("Leaving JHANDLER \n");
	iotjs_jhandler_return_null(jhandler);
}

JHANDLER_FUNCTION(sendRequest) {
	JHANDLER_CHECK_THIS(object);

	JHANDLER_CHECK_ARG(0, object);
	const iotjs_jval_t* jthis = JHANDLER_GET_ARG(0, object);

	GlobalData* globalData = (GlobalData*) iotjs_jval_get_object_native_handle(jthis);

	//Add all the headers to the easy handle
	curl_easy_setopt(globalData->curl_easy_handle, CURLOPT_HTTPHEADER, globalData->headerList);

	if (globalData->method == HTTPS_POST && globalData->contentLength != -1)
		curl_easy_setopt(globalData->curl_easy_handle, CURLOPT_POSTFIELDSIZE, globalData->contentLength );
	else if (globalData->method == HTTPS_PUT && globalData->contentLength != -1)
		curl_easy_setopt(globalData->curl_easy_handle, CURLOPT_INFILESIZE, globalData->contentLength );


	curl_multi_add_handle(globalData->curl_handle, globalData->curl_easy_handle);

	fprintf(stdout, "Added download \n");

	printf("Leaving JHANDLER \n");
	iotjs_jhandler_return_null(jhandler);
}

JHANDLER_FUNCTION(setTimeout) {
	JHANDLER_CHECK_THIS(object);
	JHANDLER_CHECK_ARGS(2, number, object);

	double ms = JHANDLER_GET_ARG(0, number);
	printf("Got timeout time in Native Code as %f \n", ms);
	const iotjs_jval_t* jthis = JHANDLER_GET_ARG(1, object);

	GlobalData* globalData = (GlobalData*) iotjs_jval_get_object_native_handle(jthis);
	//printf(" ------ Can Retrieve the tempvar in NativeCode as ----- %d \n", globalData->tempvar);
	set_timeout( (long) ms, globalData);

	printf("Leaving JHANDLER \n");
	iotjs_jhandler_return_null(jhandler);
}

JHANDLER_FUNCTION(_write) {
	printf("******************Entered _write JHANDLER *****************\n");
	JHANDLER_CHECK_THIS(object);

	JHANDLER_CHECK_ARGS(2, object, string);
	//Argument 3 can be null, so not checked here, checked below.
	JHANDLER_CHECK_ARG(3, function);

	const iotjs_jval_t* jthis = JHANDLER_GET_ARG(0, object);
	GlobalData* globalData = (GlobalData*) iotjs_jval_get_object_native_handle(jthis);

	globalData->readChunk = JHANDLER_GET_ARG(1, string);
	printf("Got Data in _write as %s \n", iotjs_string_data(&(globalData->readChunk)));
	globalData->dataToRead=true;

	if(globalData->toDestroyReadOnWrite){
		globalData->toDestroyReadOnWrite = false;
		iotjs_jval_destroy(&(globalData->readOnWrite));
		iotjs_jval_destroy(&(globalData->readCallback));
	}

	const iotjs_jval_t* callback;
	callback = iotjs_jhandler_get_arg(jhandler, 2);
	if(iotjs_jval_is_undefined(callback))
		printf("callback in _onwrite is undefined\n");

	globalData->readCallback = iotjs_jval_create_copied(callback);
	printf("Got read callback in _write \n");

	const iotjs_jval_t* onwrite = JHANDLER_GET_ARG(3, function);

	globalData->readOnWrite = iotjs_jval_create_copied(onwrite);
	globalData->toDestroyReadOnWrite = true;
	printf("Got onwrite callback in _write \n");

	//JHANDLER_CHECK_ARG(0, number);
	//double ms = JHANDLER_GET_ARG(0, number);
	//printf("Got timeout time in Native Code as %f \n", ms);

	if(globalData->isStreamWritable){
		printf("Unpaused _write \n");
		curl_easy_pause(globalData->curl_easy_handle, CURLPAUSE_CONT);
		uv_timer_stop(&(globalData->timeout));
		uv_timer_start(&(globalData->timeout), on_timeout, 1, 0);
	}

	printf("******************Leaving _write JHANDLER *****************\n");
	iotjs_jhandler_return_null(jhandler);
}

JHANDLER_FUNCTION(finishRequest) {
	printf("******************Entered finishRequest JHANDLER *****************\n");
	JHANDLER_CHECK_THIS(object);
	JHANDLER_CHECK_ARG(0, object);

	printf("1 \n");
	const iotjs_jval_t* jthis = JHANDLER_GET_ARG(0, object);
	printf("2 \n");
	GlobalData* globalData = (GlobalData*) iotjs_jval_get_object_native_handle(jthis);
	printf("3 \n");
	globalData->streamEnded=true;

	//JHANDLER_CHECK_ARG(0, number);
	//double ms = JHANDLER_GET_ARG(0, number);
	//printf("Got timeout time in Native Code as %f \n", ms);

	if(globalData->isStreamWritable){
		printf("4 \n");
		printf("Unpaused in handle_socket \n");
		curl_easy_pause(globalData->curl_easy_handle, CURLPAUSE_CONT);
		uv_timer_stop(&(globalData->timeout));
		uv_timer_start(&(globalData->timeout), on_timeout, 1, 0);
	}

	printf("******************Leaving finishRequest JHANDLER *****************\n");
	iotjs_jhandler_return_null(jhandler);
}

JHANDLER_FUNCTION(Abort) {
	printf("Entering Abort");
	JHANDLER_CHECK_THIS(object);
	JHANDLER_CHECK_ARG(0, object);

	const iotjs_jval_t* jthis = JHANDLER_GET_ARG(0, object);
	GlobalData* globalData = (GlobalData*) iotjs_jval_get_object_native_handle(jthis);

	uv_timer_stop(&(globalData->socket_timeout));
	curl_multi_remove_handle(globalData->curl_handle, globalData->curl_easy_handle);
	curl_easy_cleanup(globalData->curl_easy_handle);
	globalData->curl_easy_handle = NULL;
	destroy_GlobalData(globalData);

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
