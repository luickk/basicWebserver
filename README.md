# Simple Webserver

The webserver is a minimal implementation of a webserver (by the HTTP specification which can be found [here](https://datatracker.ietf.org/doc/html/rfc2616)) and uses only standard C libraries. The webserver has only minimal support for http features. The webservers main features is routes with individual responses and dynamic client request handling. The only supported request method is GET with no parsing of query parameters. It's a fun project of mine and although I tried to write a usable and safe application due to the complex nature of C I cannot guarantee for anything, especially not security.

The goal of the project is to write a minimal C webserver which maximizes on performance, security and memory safety.

Since part of the idea was to write something with a small footprint the whole project source code is contained in the `webserver.c` file.
The project is not meant to be used as a library but can be easily reused as one since the complete project is strictly statically written and everything is isolated from the execution context (all the functions are usable in a static library manner).

### Performance

The response and request buffer are both statically buffered. I thought about making the response buffer dynamically allocated in oder to be flexible with potential greater response payloads but decided that if this application would be used, this would happen in specific context in which the range of response sizes is not too great which would outweigh the performance decrease of a dynamically managed response.

Since I don't have any experience with writing software which has to handle huge bandwidths of requests and I still wanted to keep this project able to handle request spikes I the threads scale dynamically and are not limited by a statically sized buffer of max client threads.
All the memory allocated (by a client thread) is freed on socket close, also the actual payload is only referenced and only copied for the actual buffer send.

The parsing is implemented in a very basic manner, not leveraging any library functions. This makes maintenance more difficult and is generally challenging to read but (possibly)more performant and (possibly)more secure since it reduces operations on a few very simple procedures instead of implementing complex std lib functions.

### Security

As declared at the beginning of this projects readme it's not meant to be used in any kind of professional or production environment. I'm neither a professional nor do I have sufficient experience in order to claim this project to be secure. In order to spot common vulnerability patterns I used the static analysis tool `flawfinder`.
That said I (as always) tried to considered all the good practices and possible attack vectors. Since this webserver is not complex and only supports very few features the only superficial vector would be the http request string.

Apart from buffer overflows, 0 character escape the parsing is probably the most crucial and worrying part. In order to build something simple that does not open up too many eventualities I went with a character iterating loop which only checks for the 3 (SP,CR,LF) separating characters and copies/ parses the memory of the mem space in between. The only deciding information on which basis the parsing happens is the length (index difference)between the separation characters thus this is the only exploitable "interface" and is limited by the request buffer size. Every anomaly from the request protocol will result in an immediate abort. The introduction of malicious information in the parsed memory should be irrelevant since this memory is not interpreted in anyway afterwards(except for the versions strtok and character removal wichs common vulnerabilities were considered).

### Memory Safety

To ensure safety I applied common static and dynamic analysis tools such as `leaks`, `valgrind` and memory surveillance. The webserver leaks no memory neither at runtime nor on close and makes use of one mutex lock. When choosing data types and struct components the memory alignment has been considered. To ensure a thorough memory cleanup even if a thread crashes the threads make use of the pthread_cleanup queue to ensure that the allocated memory is properly freed.

### Testing

As already stated in the beginning the webserver is written in a way that makes it easily usable as a static library. As such all the functions are testable and tests are implemented and can be found below the main function. The tests cover all functions except for the networking and printing IO functions.

### Naming convention

Since I got introduced to static programming through go I also adopted the camelcase naming convention since I find it the most readable. I know that this is not compliant with C standard but as already said this is a fun project of mine only for learning purposes.


### Things to improve

- Fuzzing/ Payload testing <br>
There is a huge stack of untested scenarios concerning the content or other edge cases concerning the encoding and data quantity.

- Error handling <br>
Another crucial untreated part of the project is that errors are catched and returned but most of the time the project quits runtime instead of trying to recover. This is most obvious in the memory allocation handling.

## Build

The webserver can be built and run with the buildNrun bash. <br>
`bash buildNrun.sh`

Alternatively it can also be built directly by using cmake.
The project has no non standard dependencies and has been built using Clang and C14.
