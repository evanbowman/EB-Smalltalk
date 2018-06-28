# EB Smalltalk

[![Build Status](https://travis-ci.org/evanbowman/EB-Smalltalk.svg?branch=master)](https://travis-ci.org/evanbowman/EB-Smalltalk)

## Introduction

An embeddable Smalltalk runtime. The VM is ansi C, with a small footprint and no dependencies. In fact, the library doesn't even depend on libC, but needs to be passed malloc/free functions when creating an execution context. If you strip the library of debug symbols, the whole thing's around 8k and should fit in L1 cache.

The VM's nearly feature complete. Current status:
 - [x] Objects, Classes, Inheritance
 - [x] Messaging
 - [x] Instance Variables
 - [ ] Class Variables
 - [ ] Reflection
 - [x] Global Variables
 - [x] Strict, Compacting Garbage Collection
 - [x] Symbols
 - [x] Arrays
 - [x] Integers
 - [x] Boolean
 - [x] Bytecode Evaluation
