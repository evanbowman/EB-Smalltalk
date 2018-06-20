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
typedef ST_OpaqueStruct ST_Object;
typedef unsigned char ST_U8;
typedef uint16_t ST_U16;
typedef uint32_t ST_U32;
typedef int32_t ST_S32;

ST_Object ST_symb(ST_Object context, const char *symbolName);

void ST_setGlobal(ST_Object context, ST_Object symbol, ST_Object value);
ST_Object ST_getGlobal(ST_Object context, ST_Object symbol);

ST_Object ST_sendMsg(ST_Object context, ST_Object receiver, ST_Object symbol,
                     ST_U8 argc, ST_Object argv[]);

typedef ST_Object (*ST_Method)(ST_Object, ST_Object, ST_Object[]);
void ST_setMethod(ST_Object context, ST_Object targetClass, ST_Object symbol,
                  ST_Method method, ST_U8 argc);

/* Shortcuts, technically you could do all these with message sends though. */
ST_Object ST_getClass(ST_Object context, ST_Object object);
ST_Object ST_getSuper(ST_Object context, ST_Object object);
ST_Object ST_getNil(ST_Object context);
ST_Object ST_getTrue(ST_Object context);
ST_Object ST_getFalse(ST_Object context);
ST_Object ST_getInteger(ST_Object context, ST_S32 value);
ST_S32 ST_unboxInt(ST_Object integer);

/* Store the results of API calls in a local var array, to prevent the GC
   from collecting your objects. Note that Symbol Objects returned by
   ST_symb are not collected automatically, so you don't need to store them
   in local arrays.

   You might want to try this pattern:
   enum { LOC_foo, LOC_bar, LOC_count };
   ST_Object *locals = ST_pushLocals(ctx, LOC_count);
   locals[LOC_foo] = ...
   ... etc ...
   ST_popLocals(ctx);
*/
ST_Object *ST_pushLocals(ST_Object ctx, ST_Size count);
void ST_popLocals(ST_Object ctx);

typedef struct ST_Configuration {
    struct Memory {
        void *(*allocFn)(size_t);
        void (*freeFn)(void *);
        void *(*copyFn)(void *, const void *, size_t);
        void *(*setFn)(void *, int c, size_t n);
        ST_Size stackCapacity;
    } memory;
} ST_Configuration;

#define ST_DEFAULT_CONFIG                                                      \
    {                                                                          \
        { malloc, free, memcpy, memset, 1024 }                                 \
    }

ST_Object ST_createContext(const ST_Configuration *config);
void ST_destroyContext(ST_Object context);

const char *ST_Symbol_toString(ST_Object context, ST_Object symbol);

typedef struct ST_Code {
    ST_Object *symbTab;
    ST_U8 *instructions;
    ST_Size length;
} ST_Code;

ST_Code ST_VM_load(ST_Object context, const ST_U8 *data, ST_Size len);
void ST_VM_execute(ST_Object context, const ST_Code *code, ST_Size offset);

#endif /* SMALLTALK_H */

#ifdef __cplusplus
} /* extern "C" */
#endif
