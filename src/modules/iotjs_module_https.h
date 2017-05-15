/*
 * Copyright (c) 2016 Samsung Electronics Co., Ltd All Rights Reserved
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef IOTJS_MODULE_HTTPS_H_
#define IOTJS_MODULE_HTTPS_H_

#include "iotjs_def.h"
#include "iotjs_reqwrap.h"
#include "iotjs_handlewrap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>
#include <curl/curl.h>
#include <uv.h>

typedef struct {
  iotjs_reqwrap_t reqwrap;
  uv_req_t req;
} IOTJS_VALIDATED_STRUCT(iotjs_https_reqwrap_t);

iotjs_https_reqwrap_t* iotjs_https_reqwrap_create(const iotjs_jval_t* jcallback);
static void iotjs_https_reqwrap_destroy(iotjs_https_reqwrap_t* https_reqwrap);
void iotjs_https_reqwrap_dispatched(iotjs_https_reqwrap_t* https_reqwrap);
const iotjs_jval_t* iotjs_https_reqwrap_jcallback(iotjs_https_reqwrap_t* https_reqwrap);

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


//int easycurl(char* host, char* path, char* method, char* queryParams,
//		char* mBody, struct curl_slist* headerList, MemoryStruct_s* p_chunk, long* code, char* errormsg);


#endif /* IOTJS_MODULE_HTTPS_H_ */
