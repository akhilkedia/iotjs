#!/bin/bash
#Generate the CA
mkdir ./ca
cd ./ca
openssl req -out ca.pem -new -x509
#Password is password

#Generate Server Certificate/Key pair
openssl genrsa -out server.key 1024
openssl req -key server.key -new -out server.req
#No password needed. Ensure that the Common Name is same as server hsotname ("localhost" in my case)
echo 00 > file.srl
openssl x509 -req -in server.req -CA ca.pem -CAkey privkey.pem -CAserial file.srl -out server.pem

#Generate Client ccertificate/Key pair
openssl genrsa -out client.key 1024
openssl req -key client.key -new -out client.req
#Ensure that Common name is unique - email id works.
openssl x509 -req -in client.req -CA ca.pem -CAkey privkey.pem -CAserial file.srl -out client.pem

#To export the client certificate as PKCS12 for use in browers,
#openssl pkcs12 -export -clcerts -in client.pem -inkey client.key -out client.p12
