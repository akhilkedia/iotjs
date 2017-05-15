//To run this, first cd to the iotjs folder you cloned
//Then start an https server with - node ./temp/NodejsHttpsServer.js
//Then build iotjs - ./tools/build.py
//Then to finally test the HTTPS module run - ./build/x86_64-linux/debug/bin/iotjs ./temp/akhil-test.js
//WILL NEED A PROXY FREE INTERNET

var https = require('https');
var timers = require('timers');

console.log('akhil-test begin');


//************Request 1 ************
options = {
	method: 'GET',
//	host: "eu.httpbin.org",
	host: "localhost",
	path: "/",
	port: 4443,
	headers : {"Content-Type": "application/json", "cache-control": "no-cache"},
	ca: "./temp/certs/ca.pem",
	cert: "./temp/certs/client.pem",
	key: "./temp/certs/client.key"
};

var res_body = '';
var getResponseHandler = function(res) {
	console.log("Got RESPONSE Callback!");

	res.on('data', function(chunk) {
		console.log("Got Data Callback!");
		console.log(chunk);
		res_body += chunk.toString();
	});

	res.on('end', function() {
		console.log("--------In End handler--------");
		console.log(res_body);
		console.log("--------Out End handler--------");
	});
};

var req1 = https.request(options, getResponseHandler);
//Note - END MUST BE CALLED!!!!!!
req1.end();


//************ Request 2 ************

options2 = {
	method: 'POST',
	protocol: 'https:',
	host: "api.artik.cloud",
	path: "/v1.1/devices?Authorization=bearer%202ccf9479a6584522832de388c82320fc",
	//host : "http://httpbin.org/post",
	port: 443,
	highwatermark: 256,
	headers : {"Content-Length": 164, "Content-Type": "application/json", "cache-control": "no-cache"}
};
var res_body2 = '';

var getResponseHandler2 = function(res) {
	console.log("Got RESPONSE Callback!!!!!!");
	console.log(" ------- About to print header ------- ");
	for (var key in res.headers) {
		if (res.headers.hasOwnProperty(key)) {
			console.log(key + " -> " + res.headers[key]);
		}
	}
	console.log(" -------  End print header ------- ");
	console.log("The statusCode is " + res.statusCode);
	console.log("The statusMessage is " + res.statusMessage);
	//res.setTimeout(500);

	res.on('data', function(chunk) {
		console.log("Got Data Callback!!!!!!");
		console.log("Will it crash here?");
		console.log(chunk);
		res_body2 += chunk.toString();
		console.log("Toldja");
		res.pause();
		var isPaused = res.isPaused();
		console.log('Current state of stream is  - ' + isPaused);
	});

	res.on('readable', function() {
		var chunk;
		console.log('----Got Readable Callback-----');
		while (null !== (chunk = res.read())) {
			console.log(chunk);
			res_body2 += chunk.toString();
		}
		console.log('----End Readable Callback-----');
		res.resume();
		var isPaused = res.isPaused();
		console.log('Current state of stream is  - ' + isPaused);
	});
	res.on('end', function() {
		console.log("--------In End handler--------");
		console.log(res_body2);
		console.log("--------Out End handler--------");
	});
	res.on('close', function() {
		console.log("--------CONNECTION CLOSED--------");
	});
	res.on('error', function(err) {
		console.log("Got error" + err);
	});
	//TODO: Will need to check in timeout
	//res.on('timeout', function() {
	//	console.log("Got TIMEOUT!!!");
	//});
};


var req2 = https.request(options2, getResponseHandler2);
req2.on('socket', function() {
	console.log("--------In SOCKET CALLBACK--------");
});
req2.write("{\"uid\": \"cb19e910d1cb441ba5a318ea88c3ac23\",\"dtid\": \"dt6b12b870304e4dd5a88de2007f7d171e\",\"name\": \"YourTes22\",\"manifestVersion\": -1,\"manifestVersionPolicy\": \"LATEST\"");
req2.write("}");
//Note - END MUST BE CALLED!!!!!!
req2.end();



console.log('Hello End');
