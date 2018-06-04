# EB Smalltalk

[![Build Status](https://travis-ci.org/evanbowman/EB-Smalltalk.svg?branch=master)](https://travis-ci.org/evanbowman/EB-Smalltalk)

## Introduction

An embeddable Smalltalk runtime. The VM is ansi C, with a small footprint and no dependencies. In fact, the library doesn't even depend on libC, but needs to be passed malloc/free functions when creating an execution context. At just under 1300 SLOC, the VM's nearly feature complete.

Current status:
 - [x] Objects, Classes, Inheritance
 - [x] Messaging
 - [x] Instance Variables
 - [ ] Class Variables
 - [ ] Reflection
 - [x] Global Variables
 - [x] Strict Garbage Collection
 - [x] Symbols
 - [x] Arrays
 - [x] Integers
 - [x] Boolean
 - [x] Bytecode Evaluation
