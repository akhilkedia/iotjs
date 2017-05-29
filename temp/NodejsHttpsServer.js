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

//Run this file from the main Iotjs folder you cloned
const https = require('https');
const fs = require('fs');

const options = {
  key: fs.readFileSync('./temp/certs/server.key'),
  cert: fs.readFileSync('./temp/certs/server.pem'),
  // This is necessary only if using the client certificate authentication.
  requestCert: true,
  // This is necessary only if the client uses the self-signed certificate.
  ca: fs.readFileSync('./temp/certs/ca.pem')
};

var server = https.createServer(options, function(req, res) {
  var body = 'Hello World!!!!! :D :D :D \n';
  console.log('Received a request!');
  res.writeHead(200, {'Connection': 'close', 'Content-Length': body.length});
  res.end(body);
  console.log('Sent Response!');
});

server.listen(4443, () => {
  console.log('Server Started');
});
