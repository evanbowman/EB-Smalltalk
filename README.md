# EB Smalltalk

<img src="https://travis-ci.org/evanbowman/smalltalk-embedded.svg?branch=master"/> 

Work in progress, embeddable smalltalk dialect. The vm is ansi C, and is designed to have a small footprint and an easy API. The compiler and a lot of the core classes will be implemented in Smalltalk, so you can easily jetison what you don't need to cut down on program size and/or create a secure sandbox.

The vm has no dependencies, but you do need to specify malloc, free, and memcpy functions when creating an execution context.