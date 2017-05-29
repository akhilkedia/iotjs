#!/bin/bash

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

#Generate the CA
mkdir ./ca
cd ./ca
openssl req -out ca.pem -new -x509
#Password is password

#Generate Server Certificate/Key pair
openssl genrsa -out server.key 1024
openssl req -key server.key -new -out server.req
#No password needed.
#Ensure that the Common Name is same as server hostname
# ("localhost" in my case)
echo 00 > file.srl
openssl x509 -req -in server.req -CA ca.pem \
-CAkey privkey.pem -CAserial file.srl -out server.pem

#Generate Client ccertificate/Key pair
openssl genrsa -out client.key 1024
openssl req -key client.key -new -out client.req
#Ensure that Common name is unique - email id works.
openssl x509 -req -in client.req -CA ca.pem \
-CAkey privkey.pem -CAserial file.srl -out client.pem

#To export the client certificate as PKCS12 for use in browers,
#openssl pkcs12 -export -clcerts -in client.pem \
# -inkey client.key -out client.p12
