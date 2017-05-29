var http = require('http');

var server2 = http.createServer(function(request, response) {
  response.end();
});
server2.listen(3006, 5);
