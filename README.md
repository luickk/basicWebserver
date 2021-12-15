# Simple Webserver

The webserver is implemented by the HTTP specification which can be found [here](https://datatracker.ietf.org/doc/html/rfc2616) and uses only standard C libraries. The webserver has only minor support for http features.
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
