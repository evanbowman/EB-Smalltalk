#pragma once
#ifndef SMALLTALK_H
#define SMALLTALK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef size_t ST_Size;
typedef void *ST_OpaqueStruct;
typedef ST_OpaqueStruct ST_Context;
typedef ST_OpaqueStruct ST_Object;
typedef unsigned char ST_Byte;

typedef ST_Object (*ST_Method)(ST_Context, ST_Object, ST_Object[]);

ST_Object ST_Object_sendMessage(ST_Context context, ST_Object receiver,
                                ST_Object selector, ST_Byte argc,
                                ST_Object argv[]);

void ST_Object_setMethod(ST_Context context, ST_Object object,
                         ST_Object selector, ST_Method method, ST_Byte argc);

typedef struct ST_Context_Configuration {
    struct Memory {
        void *(*allocFn)(size_t);
        void (*freeFn)(void *);
        void *(*copyFn)(void *, const void *, size_t);
        void *(*setFn)(void *, int c, size_t n);
    } memory;
} ST_Context_Configuration;

ST_Context ST_createContext(const ST_Context_Configuration *config);

ST_Object ST_requestSymbol(ST_Context context, const char *symbolName);

const char *ST_Symbol_toString(ST_Context context, ST_Object symbol);

void ST_setGlobal(ST_Context context, ST_Object symbol, ST_Object value);

ST_Object ST_getGlobal(ST_Context context, ST_Object symbol);

ST_Object ST_getNilValue(ST_Context context);

ST_Object ST_getTrueValue(ST_Context context);

ST_Object ST_getFalseValue(ST_Context context);

typedef struct ST_Code {
    ST_Object *symbTab;
    ST_Size symbTabSize;
    ST_Byte *instructions;
    ST_Size length;
} ST_Code;

void ST_VM_execute(ST_Context context, const ST_Code *code, ST_Size offset);

/* ST_Code ST_VM_load(ST_Context context, const char *path); */
/* void ST_VM_store(ST_Context context, const char *path, ST_Code *code); */
/* void ST_VM_dispose(ST_Context context, ST_Code *code); */

/* Shortcuts for some common stuff */

#define ST_UNARYSEND(CONTEXT, OBJ, MESSAGE)                                    \
    ST_Object_sendMessage(CONTEXT, OBJ, ST_requestSymbol(CONTEXT, MESSAGE), 0, \
                          NULL)

#define ST_NEW(CONTEXT, CLASSNAME_CSTR)                                        \
    ST_UNARYSEND(CONTEXT, ST_getGlobal(CONTEXT, ST_requestSymbol(              \
                                                    CONTEXT, CLASSNAME_CSTR)), \
                 "new")

#define ST_INIT(CONTEXT, OBJ, ARGC, ARGV)                                      \
    ST_Object_sendMessage(CONTEXT, OBJ, ST_requestSymbol(CONTEXT, "init"),     \
                          ARGC, ARGV)

#define ST_SUBCLASS(CONTEXT, BASE_CLASSNAME_STR, DERIVED_CLASSNAME_STR)        \
    ST_setGlobal(CONTEXT, ST_requestSymbol(CONTEXT, DERIVED_CLASSNAME_STR),    \
                 ST_Object_sendMessage(                                        \
                     CONTEXT,                                                  \
                     ST_getGlobal(context, ST_requestSymbol(                   \
                                               CONTEXT, BASE_CLASSNAME_STR)),  \
                     ST_requestSymbol(CONTEXT, "subclass"), 0, NULL))

#define ST_SETMETHOD(CONTEXT, CNAME, MNAME, C_FUNC, ARGC)                      \
    ST_Object_setMethod(                                                       \
        CONTEXT, ST_getGlobal(CONTEXT, ST_requestSymbol(CONTEXT, CNAME)),      \
        ST_requestSymbol(CONTEXT, MNAME), C_FUNC, ARGC)

#endif /* SMALLTALK_H */

#ifdef __cplusplus
} /* extern "C" */
#endif
