### Platform Support

|  | Linux<br/>(Ubuntu) | Raspbian<br/>(Raspberry Pi) | Nuttx<br/>(STM32F4-Discovery) | Tizen<br/>(Artik 10) |
| :---: | :---: | :---: | :---: | :---: |
| All Apis | O/X | X | X | O |

Currently only supported for `Tizen` platform. It can also be built for Linux if `libcurl` headers are installed and not cross-compiling.

<a id="markdown-contents" name="contents"></a>
### Contents

- [https](#https)
    - [Module Functions](#module-functions)
        - [`https.request(options[, callback])`](#httpsrequestoptions-callback)
        - [`https.get(options[, callback])`](#httpsgetoptions-callback)
    - [Module Properties](#module-properties)
        - [`https.METHODS`](#httpsmethods)
- [Class: https.ClientRequest](#class-httpsclientrequest)
    - [Properties](#properties-1)
        - [`aborted`](#aborted)
    - [Events](#events-1)
        - [`'close'`](#close-1)
        - [`'response'`](#response)
        - [`'socket'`](#socket)
    - [Prototype Functions](#prototype-functions-1)
        - [`abort()`](#abort)
        - [`setTimeout(ms, cb)`](#settimeoutms-cb-1)
- [Class: https.IncomingMessage](#class-httpsincomingmessage)
    - [Properties](#properties-2)
        - [`headers`](#headers)
        - [`method`](#method)
        - [`url`](#url)
        - [`statusCode`](#statuscode)
        - [`statusMessage`](#statusmessage)
    - [Prototype Functions](#prototype-functions-2)
        - [`setTimeout(ms, cb)`](#settimeoutms-cb-2)

<a id="markdown-https" name="https"></a>
# https

IoT.js provides https to support https client enabling users to receive/send https request from easily.

<a id="markdown-module-functions" name="module-functions"></a>
<a id="markdown-httpsrequestoptions-callback" name="httpsrequestoptions-callback"></a>
### `https.request(options[, callback])`
* `options <Object>`
  * `host <string>` A domain name or IP address of server to issue the request to. Default - `'localhost'`.
  * `hostname <string>` Same as `host`.
  * `port <number>` Port of remote server to issue request to. Default - `443`
  * `method <string>` A String specifying the HTTP request method. See `https.METHODS` for a list of all supported methods. Default - `'GET'`
  * `path <string>` Request path including query string if any. Default - `'/'`
  * `headers <object>` An object specifying request headers as key-value pairs. `Content-Length` headers for `'POST'` and `'PUT'` request should also be specified here.
  * `auth <string>` Optional Basic Authentication in the form `username:password`. Used to compute HTTP Basic Authentication header.
  * `ca <string>` Optional file path to CA certificate. Allows to override system trusted CA certificates.
  * `cert <string>` Optional file path to client authentication certificate in PEM format.
  * `key <string>` Optional file path to private keys for client cert in PEM format.
* `callback <Function()>`
* Returns: `https.ClientRequest`


<a id="markdown-httpsgetoptions-callback" name="httpsgetoptions-callback"></a>
### `https.get(options[, callback])`
* `options <Object>`
* `callback <Function()>`
* Returns: `https.ClientRequest`

Same as https.request except that `https.get` automatically call `req.end()` at the end.

<a id="markdown-properties" name="module-properties"></a>
## Module Properties

<a id="markdown-httpsmethods" name="httpsmethods"></a>
### `https.METHODS`
A list of https methods supported by the parser as `String` properties of an `Object`. Currently supported methods are `'DELETE'`, `'GET'`, `'HEAD'`, `'POST'`, `'PUT'`, `'CONNECT'`, `'OPTIONS'`, `'TRACE'`.


<a id="markdown-class-httpsclientrequest" name="class-httpsclientrequest"></a>
# Class: https.ClientRequest

https.ClientRequest inherits [`Stream.writable`](IoT.js-API-Stream.md). See it's documentation to write data to an outgoing HTTP request. Notable methods are `'writable.write()'`, `'writable.end()'`, and the event `'finish'`.

<a id="markdown-properties-1" name="properties-1"></a>
## Properties

<a id="markdown-aborted" name="aborted"></a>
### `aborted`
If the request has been aborted, this contains the time at which the request was aborted in milliseconds since epoch as `Number`.

<a id="markdown-events-1" name="events-1"></a>
## Events


<a id="markdown-close-1" name="close-1"></a>
### `'close'`

This event is fired when the underlying socket is closed.

<a id="markdown-response" name="response"></a>
### `'response'`
* `response <https.IncomingMessage>`

This event is emitted when server's response header is parsed. ` https.IncomingMessage` object is passed as argument to handler.


<a id="markdown-socket" name="socket"></a>
### `'socket'`

This event is emitted when a socket is assigned to this request.

<a id="markdown-prototype-functions-1" name="prototype-functions-1"></a>
## Prototype Functions

<a id="markdown-abort" name="abort"></a>
### `abort()`

Will abort the outgoing request, dropping any data to be sent/received and destroying the underlying socket.

<a id="markdown-settimeoutms-cb-1" name="settimeoutms-cb-1"></a>
### `setTimeout(ms, cb)`

* `ms <Number>`
* `cb <Function()>`

Registers cb for `'timeout'` event and set socket's timeout value to `ms`. This event will be triggered when there is no data on the underlying socket for `ms` milliseconds.

If cb is not provided, the connection will be aborted automatically after timeout.
If you provides cb, you should handle the connection's timeout.


<a id="markdown-class-httpsincomingmessage" name="class-httpsincomingmessage"></a>
# Class: https.IncomingMessage

https.IncomingMessage inherits [`Stream.readable`](IoT.js-API-Stream.md). See it's documentation to read incoming data from an HTTP request. Notable events are `'data'`, `'close'`, `'end'` and the method `readable.read()`.

<a id="markdown-properties-2" name="properties-2"></a>
## Properties

<a id="markdown-headers" name="headers"></a>
### `headers`
http headers sent by the server as `string` key-value pairs of an `Object`.

<a id="markdown-method" name="method"></a>
### `method`
Requests method as `String`

<a id="markdown-url" name="url"></a>
### `url`
Requests URL as `String`

<a id="markdown-statuscode" name="statuscode"></a>
### `statusCode`
http response status code as a 3 digit `Number`

<a id="markdown-statusmessage" name="statusmessage"></a>
### `statusMessage`
https response status message as `String`

<a id="markdown-prototype-functions-2" name="prototype-functions-2"></a>
## Prototype Functions
<a id="markdown-settimeoutms-cb-2" name="settimeoutms-cb-2"></a>
### `setTimeout(ms, cb)`

* `ms <Number>`
* `cb <Function()>`

Registers cb for `'timeout'` event and set socket's timeout value to `ms`. This event will be triggered when there is no data on the underlying socket for `ms` milliseconds.

If cb is not provided, the connection will be aborted automatically after timeout.
If you provides cb, you should handle the connection's timeout.
