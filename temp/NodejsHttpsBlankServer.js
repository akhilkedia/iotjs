var http = require('http');

var server2 = http.createServer(function(request, response) {
  response.removeHeader('Date');
  response.removeHeader('Connection');
  response.removeHeader('Content-Length');
  response.removeHeader('Transfer-Encoding');
  response.end();
});
server2.listen(3006, 5);
