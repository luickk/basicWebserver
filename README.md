# Simple Webserver

The webserver is a minimal implementation of a webserver (by the HTTP specification which can be found [here](https://datatracker.ietf.org/doc/html/rfc2616)) and uses only standard C libraries. The webserver has only minimal support for http features. The webservers main features is routes with individual responses and dynamically scaling client request handling(threads scale dynamically according to the amount of requests with a cleanup routine and are not buffered(compromise between performance and memory efficiency). The only supported request method is GET with no parsing of query paramters.

The goal of the project is to write a minimal C webserver which maximizes on perfomance, security and memory safety.

The project was improved(or at least analyzed) by tools such as Valgrind to ensure memory safety and FlawFinder for a basic static analysis of common security issue patterns.
The goal of the project is to build a *thread safe, memory efficient and fast as well as secure webserver* by considering good practices and common safety concepts.  

The project is not meant to be used as a library but can be easily reused as one since the complete project is strictly statically written and everything is isolated from the execution context.

Feature support:
- dynamic route creation
- dynamic client request threads
- multi-client support
- buffer & file read response
- static header response
- GET method
