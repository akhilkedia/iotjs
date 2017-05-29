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
