var client = require('https_client');

var ClientRequest = exports.ClientRequest = client.ClientRequest;

exports.request = function(options, cb) {
console.log('https request');
  return new ClientRequest(options, cb);
};

var methods = {"0":"DELETE", "1":"GET", "2":"HEAD", "3":"POST", "4":"PUT", "5":"CONNECT", "6":"OPTIONS", "7":"TRACE"};
exports.METHODS = methods;

exports.get = function(options, cb) {
  var req = exports.request(options, cb);
  req.end();
  return req;
};
