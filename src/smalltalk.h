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
typedef unsigned char ST_U8;
typedef uint16_t ST_U16;
typedef uint32_t ST_U32;

ST_Object ST_symb(ST_Context context, const char *symbolName);

void ST_setGlobal(ST_Context context, ST_Object symbol, ST_Object value);
ST_Object ST_getGlobal(ST_Context context, ST_Object symbol);

ST_Object ST_sendMsg(ST_Context context, ST_Object receiver, ST_Object symbol,
                     ST_U8 argc, ST_Object argv[]);

typedef ST_Object (*ST_Method)(ST_Context, ST_Object, ST_Object[]);
void ST_setMethod(ST_Context context, ST_Object targetClass, ST_Object symbol,
                  ST_Method method, ST_U8 argc);

ST_Object ST_getClass(ST_Context context, ST_Object object);
ST_Object ST_getSuper(ST_Context context, ST_Object object);
ST_Object ST_getNil(ST_Context context);
ST_Object ST_getTrue(ST_Context context);
ST_Object ST_getFalse(ST_Context context);

void ST_GC_run(ST_Context context);

/* GC_preserve() and GC_release() may be used to prevent the garbage collector
   from collecting the result of the sendMsg() api call. Alternatively,
   if you have a lot of local variables and preserve/release would be tedious,
   you can temporarily disable the collector with GC_pause/resume. */
void ST_GC_preserve(ST_Context context, ST_Object object);
void ST_GC_release(ST_Context context, ST_Object object);
void ST_GC_pause(ST_Context context);
void ST_GC_resume(ST_Context context);

typedef struct ST_Context_Configuration {
    struct Memory {
        void *(*allocFn)(size_t);
        void (*freeFn)(void *);
        void *(*copyFn)(void *, const void *, size_t);
        void *(*setFn)(void *, int c, size_t n);
        ST_Size stackCapacity;
    } memory;
} ST_Context_Configuration;

ST_Context ST_createContext(const ST_Context_Configuration *config);
void ST_destroyContext(ST_Context context);

const char *ST_Symbol_toString(ST_Context context, ST_Object symbol);

typedef struct ST_Code {
    ST_Object *symbTab;
    ST_Size symbTabSize;
    ST_U8 *instructions;
    ST_Size length;
} ST_Code;

void ST_VM_execute(ST_Context context, const ST_Code *code, ST_Size offset);

#endif /* SMALLTALK_H */

#ifdef __cplusplus
} /* extern "C" */
#endif
