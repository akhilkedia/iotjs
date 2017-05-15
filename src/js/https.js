var client = require('https_client');

var ClientRequest = exports.ClientRequest = client.ClientRequest;

exports.request = function(options, cb) {
console.log('https request');
  return new ClientRequest(options, cb);
};


var methods = ["GET", "POST", "PUT", "DELETE", "HEAD", "CONNECT", "OPTIONS", "TRACE"];
exports.METHODS = methods;


exports.get = function(options, cb) {
  var req = exports.request(options, cb);
  req.end();
  return req;
};
