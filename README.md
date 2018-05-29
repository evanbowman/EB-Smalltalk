# EB Smalltalk

Unit Test Status: [![Build Status](https://travis-ci.org/evanbowman/EB-Smalltalk.svg?branch=master)](https://travis-ci.org/evanbowman/EB-Smalltalk)

## Introduction

Work in progress, embeddable smalltalk dialect. The vm is ansi C, and is designed to have a small footprint and an easy API. The compiler and a lot of the core classes will be implemented in Smalltalk, so you can easily remove what you don't need to cut down on program size and/or create a secure sandbox or something.

The vm has no dependencies, but you do need to specify malloc, free, and memcpy functions when creating an execution context.
