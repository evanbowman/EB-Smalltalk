#include "smalltalk.h"

struct ST_Internal_Context;

/* Note: stack operations are not currently bounds-checked. But I'm thinking
   there's a more efficient way of preventing bad access than checking every
   call. e.g. A compiler would know how many local variables are in a method,
   so lets eventually make the vm do the stack size check upon entry into a
   method. */
static void ST_pushStack(struct ST_Internal_Context *context, ST_Object val);
static void ST_popStack(struct ST_Internal_Context *context);
static ST_Object ST_refStack(struct ST_Internal_Context *context,
                             ST_Size offset);

static ST_Size ST_stackSize(struct ST_Internal_Context *context);

/* Note: For the most part, only ST_Pool_alloc/free should call ST_alloc or
   ST_free directly. */
static void *ST_alloc(ST_Context context, ST_Size size);
static void ST_free(ST_Context context, void *memory);

/*//////////////////////////////////////////////////////////////////////////////
// Helper functions
/////////////////////////////////////////////////////////////////////////////*/

typedef enum { false, true } bool;

#ifdef __GNUC__
#define UNEXPECTED(COND) __builtin_expect(COND, 0)
#else
#define UNEXPECTED(COND) COND
#endif

typedef enum ST_Cmp {
    ST_Cmp_Greater = 1,
    ST_Cmp_Less = -1,
    ST_Cmp_Eq = 0
} ST_Cmp;

int ST_strcmp(char string1[], char string2[]) {
    int i;
    for (i = 0;; i++) {
        if (string1[i] != string2[i]) {
            return string1[i] < string2[i] ? -1 : 1;
        }
        if (string1[i] == '\0') {
            return 0;
        }
    }
}

static ST_Size ST_strlen(const char *s) {
    ST_Size len = 0;
    while (*(s++))
        ++len;
    return len;
}

static void ST_strcpy(char *dst, const char *src) {
    char c;
    while ((c = *(src++)))
        *(dst++) = c;
}

static char *ST_strdup(ST_Context context, const char *s) {
    char *d = ST_alloc(context, ST_strlen(s) + 1);
    if (d == NULL)
        return NULL;
    ST_strcpy(d, s);
    return d;
}

static int ST_clamp(int value, int low, int high) {
    if (value < low)
        return low;
    else if (value > high)
        return high;
    else
        return value;
}

/*//////////////////////////////////////////////////////////////////////////////
// Pool
/////////////////////////////////////////////////////////////////////////////*/

typedef struct ST_Pool_Node {
    struct ST_Pool_Node *next;
    /* Technically the allocated memory lives in-place here in the node, but
 because this is ansi c, we don't have flexible structs yet, so you have
 to use your imagination */
} ST_Pool_Node;

typedef struct ST_Pool_Slab { struct ST_Pool_Slab *next; } ST_Pool_Slab;

typedef struct ST_Pool {
    ST_Size elemSize;
    ST_Pool_Node *freelist;
    ST_Pool_Slab *slabs;
    ST_Size lastAllocCount;
} ST_Pool;

static void ST_Pool_grow(ST_Context context, ST_Pool *pool, ST_Size count) {
    const ST_Size poolNodeSize = pool->elemSize + sizeof(ST_Pool_Node);
    const ST_Size headerSize = sizeof(ST_Pool_Slab *);
    const ST_Size allocSize = poolNodeSize * count + headerSize;
    ST_U8 *mem = ST_alloc(context, allocSize);
    ST_Size i;
    pool->lastAllocCount = count;
    for (i = headerSize; i < allocSize; i += poolNodeSize) {
        ST_Pool_Node *node = (void *)(mem + i);
        node->next = pool->freelist;
        pool->freelist = node;
    }
    ((ST_Pool_Slab *)mem)->next = pool->slabs;
    pool->slabs = (ST_Pool_Slab *)mem;
}

static void ST_Pool_init(ST_Context context, ST_Pool *pool, ST_Size elemSize,
                         ST_Size count) {
    pool->freelist = NULL;
    pool->slabs = NULL;
    pool->elemSize = elemSize;
    ST_Pool_grow(context, pool, count);
}

enum { ST_POOL_GROWTH_FACTOR = 2 };

static void *ST_Pool_alloc(ST_Context context, ST_Pool *pool) {
    ST_Pool_Node *ret;
    if (!pool->freelist) {
        ST_Pool_grow(context, pool,
                     pool->lastAllocCount * ST_POOL_GROWTH_FACTOR);
    }
    ret = pool->freelist;
    pool->freelist = pool->freelist->next;
    /* Note: advancing the node pointer by the size of the node effectively
 strips the header, thus setting the pointer to the contained element.
 see comment in ST_Pool_Node. */
    return (ST_U8 *)ret + sizeof(ST_Pool_Node);
}

/* Note: only meant for use by the garbage collector */
void ST_Pool_scan(ST_Context context, ST_Pool *pool,
                  void (*visitorFn)(ST_Context, void *)) {
    ST_Pool_Slab *slab = pool->slabs;
    ST_Size slabElemCount = pool->lastAllocCount;
    while (slab) {
        ST_U8 *slabMem = ((ST_U8 *)slab + sizeof(ST_Pool_Slab));
        ST_Size nodeSize = pool->elemSize + sizeof(ST_Pool_Node);
        ST_Size slabSize = slabElemCount * nodeSize;
        ST_Size i;
        for (i = 0; i < slabSize; i += nodeSize) {
            visitorFn(context, slabMem + i + sizeof(ST_Pool_Node));
        }
        slab = slab->next;
        slabElemCount /= ST_POOL_GROWTH_FACTOR;
    }
}

static void ST_Pool_free(ST_Context context, ST_Pool *pool, void *mem) {
    ST_Pool_Node *node = (void *)((char *)mem - sizeof(ST_Pool_Node));
    node->next = pool->freelist;
    pool->freelist = node;
}

static void ST_Pool_release(ST_Context context, ST_Pool *pool) {
    ST_Pool_Slab *slab = pool->slabs;
    while (slab) {
        ST_Pool_Slab *next = slab->next;
        ST_free(context, slab);
        slab = next;
    }
}

/*//////////////////////////////////////////////////////////////////////////////
// Context struct
/////////////////////////////////////////////////////////////////////////////*/

typedef struct ST_Internal_Context {
    ST_Context_Configuration config;
    struct ST_StringMap_Entry *symbolRegistry;
    struct ST_GlobalVarMap_Entry *globalScope;
    struct ST_Internal_Object *nilValue;
    struct ST_Internal_Object *trueValue;
    struct ST_Internal_Object *falseValue;
    struct OperandStack {
        const struct ST_Internal_Object **base;
        struct ST_Internal_Object **top;
    } operandStack;
    ST_Pool gvarNodePool;
    ST_Pool vmFramePool;
    ST_Pool methodNodePool;
    ST_Pool strmapNodePool;
    ST_Pool classPool;
} ST_Internal_Context;

/*//////////////////////////////////////////////////////////////////////////////
// Search Tree (Intrusive BST)
/////////////////////////////////////////////////////////////////////////////*/

typedef struct ST_BST_Node {
    struct ST_BST_Node *left;
    struct ST_BST_Node *right;
} ST_BST_Node;

static void ST_BST_Node_init(ST_BST_Node *node) {
    node->left = NULL;
    node->right = NULL;
}

static void ST_BST_splay(ST_BST_Node **t, void *key,
                         ST_Cmp (*comparator)(void *, void *)) {
    ST_BST_Node N, *l, *r, *y;
    N.left = N.right = NULL;
    l = r = &N;
    while (true) {
        const ST_Cmp compareResult = comparator(*t, key);
        if (compareResult == ST_Cmp_Greater) {
            if ((*t)->left != NULL &&
                comparator((*t)->left, key) == ST_Cmp_Greater) {
                y = (*t)->left;
                (*t)->left = y->right;
                y->right = *t;
                *t = y;
            }
            if ((*t)->left == NULL)
                break;
            r->left = *t;
            r = *t;
            *t = (*t)->left;
        } else if (compareResult == ST_Cmp_Less) {
            if ((*t)->right != NULL &&
                comparator((*t)->right, key) == ST_Cmp_Less) {
                y = (*t)->right;
                (*t)->right = y->left;
                y->left = *t;
                *t = y;
            }
            if ((*t)->right == NULL)
                break;
            l->right = *t;
            l = *t;
            *t = (*t)->right;
        } else
            break;
    }
    l->right = (*t)->left;
    r->left = (*t)->right;
    (*t)->left = N.right;
    (*t)->right = N.left;
}

static bool ST_BST_insert(ST_BST_Node **root, ST_BST_Node *node,
                          ST_Cmp (*comparator)(void *, void *)) {
    ST_BST_Node *current = *root;
    if (UNEXPECTED(!*root)) {
        *root = node;
        return true;
    }
    while (true) {
        switch (comparator(current, node)) {
        case ST_Cmp_Eq: /* (Key exists) */
            return false;
        case ST_Cmp_Greater:
            if (current->left) {
                current = current->left;
                break;
            } else {
                current->left = node;
                return true;
            }
        case ST_Cmp_Less:
            if (current->right) {
                current = current->right;
                break;
            } else {
                current->right = node;
                return true;
            }
        }
    };
}

static ST_BST_Node *ST_BST_find(ST_BST_Node **root, void *key,
                                ST_Cmp (*comp)(void *, void *)) {
    ST_BST_Node *current = *root;
    if (UNEXPECTED(!*root))
        return NULL;
    while (true) {
        switch (comp(current, key)) {
        case ST_Cmp_Eq: /* (found) */
            return current;
        case ST_Cmp_Greater:
            if (current->left) {
                current = current->left;
                break;
            } else
                return NULL;
        case ST_Cmp_Less:
            if (current->right) {
                current = current->right;
                break;
            } else
                return NULL;
        }
    }
}

/*//////////////////////////////////////////////////////////////////////////////
// Symbol Map
/////////////////////////////////////////////////////////////////////////////*/

typedef struct ST_SymbolMap_Entry {
    ST_BST_Node node;
    ST_Object symbol;
} ST_SymbolMap_Entry;

static ST_Cmp ST_SymbolMap_comparator(void *left, void *right) {
    ST_SymbolMap_Entry *leftEntry = left;
    ST_SymbolMap_Entry *rightEntry = right;
    if (leftEntry->symbol > rightEntry->symbol) {
        return ST_Cmp_Less;
    } else if (leftEntry->symbol < rightEntry->symbol) {
        return ST_Cmp_Greater;
    } else {
        return ST_Cmp_Eq;
    }
}

/*//////////////////////////////////////////////////////////////////////////////
// Method
/////////////////////////////////////////////////////////////////////////////*/

typedef ST_Method ST_PrimitiveMethod;
typedef struct ST_CompiledMethod {
    const ST_Code *source;
    ST_Size offset;
} ST_CompiledMethod;

typedef struct ST_Internal_Method {
    enum { ST_METHOD_TYPE_PRIMITIVE, ST_METHOD_TYPE_COMPILED } type;
    union {
        ST_PrimitiveMethod primitiveMethod;
        ST_CompiledMethod compiledMethod;
    } payload;
    ST_U8 argc;
} ST_Internal_Method;

/*//////////////////////////////////////////////////////////////////////////////
// Object
/////////////////////////////////////////////////////////////////////////////*/

typedef struct ST_MethodMap_Entry {
    ST_SymbolMap_Entry header;
    ST_Internal_Method method;
} ST_MethodMap_Entry;

enum ST_GC_Mask {
    ST_GC_MASK_ALIVE = 1,
    ST_GC_MASK_MARKED = 1 << 1,
    ST_GC_MASK_PERSISTENT = 1 << 2
};

typedef struct ST_Internal_Object {
    struct ST_Class *class;
    ST_U8 gcMask;
    /* Note:
   Unless an object is a class, there's an instance variable array inlined
   at the end of the struct.
   struct ST_Internal_Object *InstanceVariables[] */
} ST_Internal_Object;

static ST_Internal_Object **ST_Object_getIVars(ST_Internal_Object *object) {
    return (void *)((ST_U8 *)object + sizeof(ST_Internal_Object));
}

ST_Object ST_getClass(ST_Context context, ST_Object object) {
    return ((ST_Internal_Object *)object)->class;
}

typedef struct ST_Class {
    ST_Internal_Object object;
    ST_MethodMap_Entry *methodTree;
    struct ST_Class *super;
    ST_U16 instanceVariableCount;
    ST_Pool instancePool;
    void (*finalizer)(ST_Context, ST_Object);
} ST_Class;

ST_U16 ST_getIVarCount(ST_Context context, ST_Object object) {
    return ((ST_Internal_Object *)object)->class->instanceVariableCount;
}

ST_Object ST_getSuper(ST_Context context, ST_Object object) {
    return ((ST_Internal_Object *)object)->class->super;
}

ST_Internal_Object *ST_Class_makeInstance(ST_Internal_Context *context,
                                          ST_Class *class) {
    ST_Internal_Object *instance = ST_Pool_alloc(context, &class->instancePool);
    ST_Internal_Object **ivars = ST_Object_getIVars(instance);
    ST_Size i;
    for (i = 0; i < class->instanceVariableCount; ++i) {
        ivars[i] = ST_getNilValue(context);
    }
    instance->class = class;
    instance->gcMask = ST_GC_MASK_ALIVE;
    return instance;
}

ST_Class *ST_Class_subclass(ST_Internal_Context *context, ST_Class *class,
                            ST_Size instanceVariableCount,
                            ST_Size classVariableCount,
                            ST_Size instancePoolHint) {
    ST_Class *sub =
        ST_Pool_alloc(context, &((ST_Internal_Context *)context)->classPool);
    sub->object.class = sub;
    sub->object.gcMask = ST_GC_MASK_ALIVE;
    sub->super = class;
    sub->instanceVariableCount =
        ((ST_Class *)class)->instanceVariableCount + instanceVariableCount;
    sub->methodTree = NULL;
    sub->finalizer = NULL;
    ST_Pool_init(context, &sub->instancePool,
                 sizeof(ST_Internal_Object) +
                     sizeof(ST_Internal_Object *) * sub->instanceVariableCount,
                 instancePoolHint);
    return sub;
}

static bool ST_isClass(ST_Internal_Object *object) {
    return (ST_Class *)object == object->class;
}

static bool ST_Object_hasIVar(ST_Context context, ST_Object object,
                              ST_U16 position) {
    if (ST_isClass(object)) {
        return false;
    }
    if (position >= ST_getIVarCount(context, object)) {
        return false;
    }
    return true;
}

ST_Object ST_getIVar(ST_Context context, ST_Object object, ST_U16 position) {
    if (!ST_Object_hasIVar(context, object, position))
        return ST_getNilValue(context);
    return ST_Object_getIVars(object)[position];
}

void ST_setIVar(ST_Context context, ST_Object object, ST_U16 position,
                ST_Object value) {
    if (!ST_Object_hasIVar(context, object, position)) {
        /* FIXME: raise error! */
        return;
    }
    ST_Object_getIVars(object)[position] = value;
}

static ST_Internal_Method *
ST_Internal_Object_getMethod(ST_Context context, ST_Internal_Object *obj,
                             ST_Internal_Object *selector) {
    ST_Class *currentClass = obj->class;
    while (true) {
        ST_SymbolMap_Entry searchTmpl;
        ST_BST_Node *found;
        searchTmpl.symbol = selector;
        found = ST_BST_find((ST_BST_Node **)&currentClass->methodTree,
                            &searchTmpl, ST_SymbolMap_comparator);
        if (found) {
            return &((ST_MethodMap_Entry *)found)->method;
        } else {
            /* Note: the current dummy implementation of metaclasses works
         by having a class hold a circular reference to itself, so we
         need to test self/super-equality before walking up the meta-class
         hierarchy. */
            if (currentClass->super != currentClass) {
                currentClass = currentClass->super;
            } else {
                return NULL;
            }
        }
    }
}

static void ST_failedMethodLookup(ST_Context context, ST_Object receiver,
                                  ST_Object selector) {
    ST_Object err = ST_NEW(context, "MessageNotUnderstood");
    ST_sendMessage(context, receiver,
                   ST_requestSymbol(context, "doesNotUnderstand:"), 1, &err);
}

ST_Object ST_sendMessage(ST_Context context, ST_Object receiver,
                         ST_Object selector, ST_U8 argc, ST_Object argv[]) {
    ST_Internal_Method *method =
        ST_Internal_Object_getMethod(context, receiver, selector);
    if (method) {
        switch (method->type) {
        case ST_METHOD_TYPE_PRIMITIVE:
            if (argc != method->argc) {
                /* FIXME: wrong number of args */
                return ST_getNilValue(context);
            }
            return method->payload.primitiveMethod(context, receiver, argv);

        case ST_METHOD_TYPE_COMPILED: {
            ST_U8 i;
            ST_Object result;
            for (i = 0; i < argc; ++i) {
                ST_pushStack(context, argv[i]);
            }
            ST_VM_execute(context, method->payload.compiledMethod.source,
                          method->payload.compiledMethod.offset);
            result = ST_refStack(context, 0);
            ST_popStack(context);        /* Pop result */
            for (i = 0; i < argc; ++i) { /* Need to pop argv too */
                ST_popStack(context);
            }
            return result;
        }
        }
    }
    ST_failedMethodLookup(context, receiver, selector);
    return ST_getNilValue(context);
}

static bool ST_Class_insertMethodEntry(ST_Context context, ST_Class *class,
                                       ST_MethodMap_Entry *entry) {
    if (!ST_BST_insert((ST_BST_Node **)&((ST_Class *)class)->methodTree,
                       &entry->header.node, ST_SymbolMap_comparator)) {
        return false;
    }
    ST_BST_splay((ST_BST_Node **)&((ST_Class *)class)->methodTree,
                 &entry->header.node, ST_SymbolMap_comparator);
    return true;
}

void ST_setMethod(ST_Context context, ST_Object object, ST_Object selector,
                  ST_PrimitiveMethod primitiveMethod, ST_U8 argc) {
    ST_Pool *methodPool = &((ST_Internal_Context *)context)->methodNodePool;
    ST_MethodMap_Entry *entry = ST_Pool_alloc(context, methodPool);
    ST_BST_Node_init((ST_BST_Node *)entry);
    entry->header.symbol = selector;
    entry->method.type = ST_METHOD_TYPE_PRIMITIVE;
    entry->method.payload.primitiveMethod = primitiveMethod;
    entry->method.argc = argc;
    if (!ST_Class_insertMethodEntry(
            context, ((ST_Internal_Object *)object)->class, entry)) {
        ST_Pool_free(context, methodPool, entry);
    }
}

/*//////////////////////////////////////////////////////////////////////////////
// Context
/////////////////////////////////////////////////////////////////////////////*/

static void *ST_alloc(ST_Context context, ST_Size size) {
    void *mem = ((ST_Internal_Context *)context)->config.memory.allocFn(size);
    if (UNEXPECTED(!mem)) {
        /* TODO... */
    }
    return mem;
}

static void ST_free(ST_Context context, void *memory) {
    ((ST_Internal_Context *)context)->config.memory.freeFn(memory);
}

typedef struct ST_StringMap_Entry {
    ST_BST_Node nodeHeader;
    char *key;
    void *value;
} ST_StringMap_Entry;

static ST_Cmp ST_StringMap_comparator(void *left, void *right) {
    ST_StringMap_Entry *lhsEntry = left;
    ST_StringMap_Entry *rhsEntry = right;
    return ST_clamp(ST_strcmp(lhsEntry->key, rhsEntry->key), ST_Cmp_Less,
                    ST_Cmp_Greater);
}

typedef struct ST_GlobalVarMap_Entry {
    ST_SymbolMap_Entry header;
    ST_Internal_Object *value;
} ST_GlobalVarMap_Entry;

static void ST_pushStack(ST_Internal_Context *context, ST_Object val) {
    *(context->operandStack.top++) = val;
}

static void ST_popStack(struct ST_Internal_Context *context) {
    --context->operandStack.top;
}

static ST_Object ST_refStack(struct ST_Internal_Context *context,
                             ST_Size offset) {
    return *(context->operandStack.top - offset);
}

static ST_Size ST_stackSize(struct ST_Internal_Context *context) {
    return (const ST_Internal_Object **)context->operandStack.top -
           context->operandStack.base;
}

ST_Object ST_getGlobal(ST_Context context, ST_Object symbol) {
    ST_BST_Node *found;
    ST_BST_Node **globalScope =
        (void *)&((ST_Internal_Context *)context)->globalScope;
    ST_SymbolMap_Entry searchTmpl;
    searchTmpl.symbol = symbol;
    found = ST_BST_find(globalScope, &searchTmpl, ST_SymbolMap_comparator);
    if (UNEXPECTED(!found)) {

        return ST_getNilValue(context);
    }
    ST_BST_splay(globalScope, &symbol, ST_SymbolMap_comparator);
    return ((ST_GlobalVarMap_Entry *)found)->value;
}

void ST_setGlobal(ST_Context context, ST_Object symbol, ST_Object object) {
    ST_Internal_Context *ctx = context;
    ST_GlobalVarMap_Entry *entry = ST_Pool_alloc(ctx, &ctx->gvarNodePool);
    ST_BST_Node_init((ST_BST_Node *)entry);
    entry->header.symbol = symbol;
    entry->value = object;
    if (!ST_BST_insert((ST_BST_Node **)&ctx->globalScope, &entry->header.node,
                       ST_SymbolMap_comparator)) {
        ST_Pool_free(context, &ctx->gvarNodePool, entry);
    }
}

ST_Object ST_getNilValue(ST_Context context) {
    return ((ST_Internal_Context *)context)->nilValue;
}

ST_Object ST_getTrueValue(ST_Context context) {
    return ((ST_Internal_Context *)context)->trueValue;
}

ST_Object ST_getFalseValue(ST_Context context) {
    return ((ST_Internal_Context *)context)->falseValue;
}

ST_Object ST_requestSymbol(ST_Context context, const char *symbolName) {
    ST_Internal_Context *extCtx = context;
    ST_BST_Node *found;
    ST_StringMap_Entry searchTmpl;
    ST_StringMap_Entry *newEntry;
    searchTmpl.key = (char *)symbolName;
    found = ST_BST_find((ST_BST_Node **)&extCtx->symbolRegistry, &searchTmpl,
                        ST_StringMap_comparator);
    if (found) {
        return (ST_Object)((ST_StringMap_Entry *)found)->value;
    }
    newEntry = ST_Pool_alloc(context, &extCtx->strmapNodePool);
    ST_BST_Node_init((ST_BST_Node *)newEntry);
    newEntry->key = ST_strdup(context, symbolName);
    newEntry->value = ST_NEW(context, "Symbol");
    if (!ST_BST_insert((ST_BST_Node **)&extCtx->symbolRegistry,
                       &newEntry->nodeHeader, ST_StringMap_comparator)) {
        ST_free(context, newEntry->key);
        ST_Pool_free(context, &extCtx->strmapNodePool, newEntry);
        return ST_getNilValue(context);
    }
    return newEntry->value;
}

static const char *ST_recDecodeSymbol(ST_StringMap_Entry *tree,
                                      ST_Object symbol) {
    if ((ST_Object)tree->value == symbol)
        return tree->key;
    if (tree->nodeHeader.left) {
        const char *found = ST_recDecodeSymbol(
            ((ST_StringMap_Entry *)tree->nodeHeader.left), symbol);
        if (found)
            return found;
    }
    if (tree->nodeHeader.right) {
        const char *found = ST_recDecodeSymbol(
            ((ST_StringMap_Entry *)tree->nodeHeader.right), symbol);
        if (found)
            return found;
    }
    return NULL;
}

const char *ST_Symbol_toString(ST_Context context, ST_Object symbol) {
    ST_Internal_Context *ctx = context;
    ST_StringMap_Entry *symbolRegistry = ctx->symbolRegistry;
    if (symbolRegistry) {
        return ST_recDecodeSymbol(symbolRegistry, symbol);
    }
    return NULL;
}

/*//////////////////////////////////////////////////////////////////////////////
// VM
/////////////////////////////////////////////////////////////////////////////*/

typedef enum ST_VM_Opcode {
    ST_VM_OP_GETGLOBAL,
    ST_VM_OP_SETGLOBAL,
    ST_VM_OP_PUSHNIL,
    ST_VM_OP_PUSHTRUE,
    ST_VM_OP_PUSHFALSE,
    ST_VM_OP_PUSHSYMBOL,
    ST_VM_OP_SENDMESSAGE,
    ST_VM_OP_SETMETHOD,
    ST_VM_OP_RETURN,
    ST_VM_OP_GETIVAR,
    ST_VM_OP_SETIVAR,
    ST_VM_OP_DUP,
    ST_VM_OP_POP,
    ST_VM_OP_SWAP,
    ST_VM_OP_COUNT
} ST_VM_Opcode;

static void ST_VM_invokePrimitiveMethod_NArg(ST_Internal_Context *context,
                                             ST_Object receiver,
                                             ST_Internal_Method *method) {
    ST_Object argv[UINT8_MAX];
    ST_U8 i;
    for (i = 0; i < method->argc; ++i) {
        argv[i] = ST_refStack(context, 0);
        ST_popStack(context);
    }
    ST_pushStack(context,
                 method->payload.primitiveMethod(context, receiver, argv));
}

typedef struct ST_VM_Frame {
    ST_Size ip;
    ST_Size bp;
    const ST_Code *code;
    struct ST_VM_Frame *parent;
} ST_VM_Frame;

static ST_U16 ST_readLE16(const ST_VM_Frame *f, ST_Size offset) {
    const ST_U8 *base = f->code->instructions + f->ip + offset;
    return ((ST_U16)*base) | ((ST_U16) * (base + 1) << 8);
}

static ST_U32 ST_readLE32(const ST_VM_Frame *f, ST_Size offset) {
    const ST_U8 *base = f->code->instructions + f->ip + offset;
    return ((ST_U32)*base) | ((ST_U32) * (base + 1) << 8) |
           ((ST_U32) * (base + 2) << 16) | ((ST_U32) * (base + 3) << 24);
}

enum { ST_OPCODE_SIZE = sizeof(ST_U8) };

static void ST_Internal_VM_execute(ST_Internal_Context *context,
                                   ST_VM_Frame *frame) {
    while (frame->ip < frame->code->length) {
        switch (frame->code->instructions[frame->ip]) {
        case ST_VM_OP_GETGLOBAL: {
            ST_Object gVarSymb = frame->code->symbTab[ST_readLE16(frame, 1)];
            ST_Object global = ST_getGlobal(context, gVarSymb);
            ST_pushStack(context, global);
            frame->ip += ST_OPCODE_SIZE + sizeof(ST_U16);
        } break;

        case ST_VM_OP_SETGLOBAL: {
            ST_Object gVarSymb = frame->code->symbTab[ST_readLE16(frame, 1)];
            ST_setGlobal(context, gVarSymb, ST_refStack(context, 0));
            ST_popStack(context);
            frame->ip += ST_OPCODE_SIZE + sizeof(ST_U16);
        } break;

        case ST_VM_OP_GETIVAR: {
            ST_U16 ivarIndex = ST_readLE16(frame, 1);
            ST_Object target = ST_refStack(context, 0);
            ST_popStack(context);
            ST_pushStack(context, ST_Object_getIVars(target)[ivarIndex]);
            frame->ip += ST_OPCODE_SIZE + sizeof(ST_U16);
        } break;

        case ST_VM_OP_SETIVAR: {
            ST_U16 ivarIndex = ST_readLE16(frame, 1);
            ST_Object target = ST_refStack(context, 0);
            ST_Object value = ST_refStack(context, 1);
            ST_popStack(context);
            ST_popStack(context);
            ST_Object_getIVars(target)[ivarIndex] = value;
            frame->ip += ST_OPCODE_SIZE + sizeof(ST_U16);
        } break;

        case ST_VM_OP_PUSHNIL:
            ST_pushStack(context, ST_getNilValue(context));
            frame->ip += ST_OPCODE_SIZE;
            break;

        case ST_VM_OP_PUSHTRUE:
            ST_pushStack(context, ST_getTrueValue(context));
            frame->ip += ST_OPCODE_SIZE;
            break;

        case ST_VM_OP_PUSHFALSE:
            ST_pushStack(context, ST_getFalseValue(context));
            frame->ip += ST_OPCODE_SIZE;
            break;

        case ST_VM_OP_PUSHSYMBOL:
            ST_pushStack(context, frame->code->symbTab[ST_readLE16(frame, 1)]);
            frame->ip += ST_OPCODE_SIZE + sizeof(ST_U16);
            break;

        case ST_VM_OP_SENDMESSAGE: {
            const ST_Object selector =
                frame->code->symbTab[ST_readLE16(frame, 1)];
            ST_Object receiver = ST_refStack(context, 0);
            ST_Internal_Method *method =
                ST_Internal_Object_getMethod(context, receiver, selector);
            frame->ip += ST_OPCODE_SIZE + sizeof(ST_U16);
            if (method) {
                switch (method->type) {
                case ST_METHOD_TYPE_PRIMITIVE:
                    ST_popStack(context); /* pop receiver */
                    ST_VM_invokePrimitiveMethod_NArg(context, receiver, method);
                    break;

                case ST_METHOD_TYPE_COMPILED: {
                    ST_VM_Frame *newFrame =
                        ST_Pool_alloc(context, &context->vmFramePool);
                    newFrame->ip = method->payload.compiledMethod.offset;
                    newFrame->code = method->payload.compiledMethod.source;
                    newFrame->bp = ST_stackSize(context);
                    newFrame->parent = frame;
                    frame = newFrame;
                } break;
                }
            } else {
                ST_failedMethodLookup(context, receiver, selector);
            }
        } break;

        case ST_VM_OP_SETMETHOD: {
            const ST_Object selector =
                frame->code->symbTab[ST_readLE16(frame, 1)];
            const ST_Object target = ST_refStack(context, 0);
            const ST_U8 argc = frame->code->instructions[frame->ip + 3];
            ST_MethodMap_Entry *entry =
                ST_Pool_alloc(context, &context->methodNodePool);
            ST_BST_Node_init((ST_BST_Node *)entry);
            frame->ip += sizeof(ST_U8) + sizeof(ST_U16);
            entry->header.symbol = selector;
            entry->method.type = ST_METHOD_TYPE_COMPILED;
            entry->method.argc = argc;
            entry->method.payload.compiledMethod.source = frame->code;
            entry->method.payload.compiledMethod.offset =
                frame->ip + sizeof(ST_U32) + 1;
            if (!ST_Class_insertMethodEntry(context, target, entry)) {
                ST_Pool_free(context, &context->methodNodePool, entry);
            }
            ST_popStack(context);
            frame->ip +=
                ST_readLE32(frame, 1) + sizeof(ST_U32) + ST_OPCODE_SIZE;
        } break;

        case ST_VM_OP_RETURN: {
            ST_VM_Frame *completeFrame = frame;
            ST_Object ret = ST_refStack(context, 0);
            ST_Size i;
            const ST_Size stackDiff = frame->bp - frame->parent->bp;
            if (frame->bp < frame->parent->bp) {
                /* FIXME */
            }
            for (i = 0; i < stackDiff; ++i) {
                ST_popStack(context);
            }
            ST_pushStack(context, ret);
            frame = frame->parent;
            ST_Pool_free(context, &context->vmFramePool, completeFrame);
            /* NOTE: we jumped frames, reverting back to the instruction pointer
   before the call, which is why we don't increment frame->ip. */
        } break;

        case ST_VM_OP_DUP: {
            ST_Object top = ST_refStack(context, 0);
            ST_pushStack(context, top);
            frame->ip += ST_OPCODE_SIZE;
        } break;

        case ST_VM_OP_POP: {
            ST_popStack(context);
            frame->ip += ST_OPCODE_SIZE;
            break;
        }

        default:
            return; /* FIXME */
        }
    }
}

void ST_VM_execute(ST_Context context, const ST_Code *code, ST_Size offset) {
    ST_VM_Frame rootFrame;
    rootFrame.ip = offset;
    rootFrame.parent = NULL;
    rootFrame.code = code;
    rootFrame.bp = ST_stackSize(context);
    ST_Internal_VM_execute(context, &rootFrame);
}

/*//////////////////////////////////////////////////////////////////////////////
// Integer
/////////////////////////////////////////////////////////////////////////////*/

typedef int32_t ST_Integer_Rep;
typedef struct ST_Integer {
    ST_Internal_Object object;
    ST_Integer_Rep value;
} ST_Integer;

static bool ST_Integer_typecheck(ST_Internal_Object *lhs,
                                 ST_Internal_Object *rhs) {
    return lhs->class == rhs->class;
}

static ST_Object ST_Integer_add(ST_Context ctx, ST_Object self,
                                ST_Object argv[]) {
    ST_Integer *ret;
    if (!ST_Integer_typecheck(self, argv[0]))
        return ST_getNilValue(ctx);
    ret = (ST_Integer *)ST_Class_makeInstance(ctx, ST_getClass(ctx, self));
    ret->value = ((ST_Integer *)self)->value + ((ST_Integer *)argv[0])->value;
    return ret;
}

static ST_Object ST_Integer_sub(ST_Context ctx, ST_Object self,
                                ST_Object argv[]) {
    ST_Integer *ret;
    if (!ST_Integer_typecheck(self, argv[0]))
        return ST_getNilValue(ctx);
    ret = (ST_Integer *)ST_Class_makeInstance(ctx, ST_getClass(ctx, self));
    ret->value = ((ST_Integer *)self)->value - ((ST_Integer *)argv[0])->value;
    return ret;
}

static ST_Object ST_Integer_mul(ST_Context ctx, ST_Object self,
                                ST_Object argv[]) {
    ST_Integer *ret;
    if (!ST_Integer_typecheck(self, argv[0]))
        return ST_getNilValue(ctx);
    ret = (ST_Integer *)ST_Class_makeInstance(ctx, ST_getClass(ctx, self));
    ret->value = ((ST_Integer *)self)->value * ((ST_Integer *)argv[0])->value;
    return ret;
}

static ST_Object ST_Integer_div(ST_Context ctx, ST_Object self,
                                ST_Object argv[]) {
    ST_Integer *ret;
    if (!ST_Integer_typecheck(self, argv[0]))
        return ST_getNilValue(ctx);
    ret = (ST_Integer *)ST_Class_makeInstance(ctx, ST_getClass(ctx, self));
    ret->value = ((ST_Integer *)self)->value / ((ST_Integer *)argv[0])->value;
    return ret;
}

static ST_Object ST_Integer_rawGet(ST_Context context, ST_Object self,
                                   ST_Object argv[]) {
    return (ST_Object)(intptr_t)((ST_Integer *)self)->value;
}

static ST_Object ST_Integer_rawSet(ST_Context context, ST_Object self,
                                   ST_Object argv[]) {
    ((ST_Integer *)self)->value = (ST_Integer_Rep)(intptr_t)argv[0];
    return ST_getNilValue(context);
}

static void ST_initInteger(ST_Internal_Context *ctx) {
    ST_Class *cObj = ST_getGlobal(ctx, ST_requestSymbol(ctx, "Object"));
    ST_Class *cInt = ST_Pool_alloc(ctx, &ctx->classPool);
    cInt->instanceVariableCount = 0;
    ST_Pool_init(ctx, &cInt->instancePool, sizeof(ST_Integer), 128);
    cInt->methodTree = NULL;
    cInt->finalizer = NULL;
    cInt->super = cObj;
    cInt->object.class = cInt;
    cInt->object.gcMask = ST_GC_MASK_ALIVE;
    ST_setMethod(ctx, cInt, ST_requestSymbol(ctx, "+"), ST_Integer_add, 1);
    ST_setMethod(ctx, cInt, ST_requestSymbol(ctx, "-"), ST_Integer_sub, 1);
    ST_setMethod(ctx, cInt, ST_requestSymbol(ctx, "*"), ST_Integer_mul, 1);
    ST_setMethod(ctx, cInt, ST_requestSymbol(ctx, "/"), ST_Integer_div, 1);
    ST_setMethod(ctx, cInt, ST_requestSymbol(ctx, "rawSet:"), ST_Integer_rawSet,
                 1);
    ST_setMethod(ctx, cInt, ST_requestSymbol(ctx, "rawGet"), ST_Integer_rawGet,
                 0);
    ST_setGlobal(ctx, ST_requestSymbol(ctx, "Integer"), cInt);
}

/*//////////////////////////////////////////////////////////////////////////////
// Array
/////////////////////////////////////////////////////////////////////////////*/

typedef struct ST_Array {
    ST_Internal_Object object;
    ST_Integer_Rep length;
    ST_Object *data;
} ST_Array;

static ST_Object ST_Array_new(ST_Context context, ST_Object self,
                              ST_Object argv[]) {
    ST_Object rgetSymb = ST_requestSymbol(context, "rawGet");
    ST_Array *arr = (ST_Array *)ST_Class_makeInstance(context, self);
    ST_Integer_Rep i;
    arr->length = (intptr_t)ST_sendMessage(context, argv[0], rgetSymb, 0, NULL);
    arr->data = ST_alloc(context, sizeof(ST_Object) * arr->length);
    for (i = 0; i < arr->length; ++i) {
        arr->data[i] = ST_getNilValue(context);
    }
    return arr;
}

static ST_Object ST_Array_at(ST_Context context, ST_Object self,
                             ST_Object argv[]) {
    ST_Object rgetSymb = ST_requestSymbol(context, "rawGet");
    ST_Integer_Rep index =
        (intptr_t)ST_sendMessage(context, argv[0], rgetSymb, 0, NULL);
    if (index > -1 && index < ((ST_Array *)self)->length) {
        return ((ST_Array *)self)->data[index];
    }
    return ST_getNilValue(context);
}

static ST_Object ST_Array_set(ST_Context context, ST_Object self,
                              ST_Object argv[]) {
    ST_Object rgetSymb = ST_requestSymbol(context, "rawGet");
    ST_Integer_Rep index =
        (intptr_t)ST_sendMessage(context, argv[0], rgetSymb, 0, NULL);
    if (index > -1 && index < ((ST_Array *)self)->length) {
        ((ST_Array *)self)->data[index] = argv[1];
    }
    return ST_getNilValue(context);
}

static ST_Object ST_Array_len(ST_Context context, ST_Object self,
                              ST_Object argv[]) {
    ST_Object result = ST_NEW(context, "Integer");
    ST_Object rawsetSymb = ST_requestSymbol(context, "rawSet:");
    ST_Object rsetArgs[1];
    rsetArgs[0] = (ST_Object)(intptr_t)((ST_Array *)self)->length;
    ST_sendMessage(context, result, rawsetSymb, 1, rsetArgs);
    return result;
}

static void ST_finalizeArray(ST_Context context, ST_Object object) {
    ST_Array *array = object;
    ST_free(context, array->data);
}

static void ST_initArray(ST_Internal_Context *ctx) {
    ST_Class *cObj = ST_getGlobal(ctx, ST_requestSymbol(ctx, "Object"));
    ST_Class *cArr = ST_Pool_alloc(ctx, &ctx->classPool);
    cArr->instanceVariableCount = 0;
    ST_Pool_init(ctx, &cArr->instancePool, sizeof(ST_Array), 64);
    cArr->methodTree = NULL;
    cArr->finalizer = ST_finalizeArray;
    cArr->super = cObj;
    cArr->object.class = cArr;
    cArr->object.gcMask = ST_GC_MASK_ALIVE;
    ST_setMethod(ctx, cArr, ST_requestSymbol(ctx, "length"), ST_Array_len, 0);
    ST_setMethod(ctx, cArr, ST_requestSymbol(ctx, "new:"), ST_Array_new, 1);
    ST_setMethod(ctx, cArr, ST_requestSymbol(ctx, "at:"), ST_Array_at, 1);
    ST_setMethod(ctx, cArr, ST_requestSymbol(ctx, "at:put:"), ST_Array_set, 2);
    ST_setGlobal(ctx, ST_requestSymbol(ctx, "Array"), cArr);
}

/*//////////////////////////////////////////////////////////////////////////////
// GC
//
// Here we have an old-fashioned mark and sweep collector. Fine for our
// purposes.
//
/////////////////////////////////////////////////////////////////////////////*/

void ST_GC_run(ST_Internal_Context *context) {}

void ST_GC_mark(ST_Internal_Context *context) {}

void ST_GC_sweepVisitInstance(ST_Context context, void *instanceMem) {
    ST_Internal_Object *obj = instanceMem;
    if (obj->gcMask & ST_GC_MASK_ALIVE) {
        if (obj->gcMask & ST_GC_MASK_MARKED ||
            obj->gcMask & ST_GC_MASK_PERSISTENT) {
            obj->gcMask &= ~ST_GC_MASK_MARKED;
        } else {
            if (obj->class->finalizer) {
                obj->class->finalizer(context, obj);
            }
            ST_Pool_free(context, &obj->class->instancePool, obj);
        }
    }
}

void ST_GC_sweepVisitClass(ST_Context context, void *classMem) {
    ST_Class *class = classMem;
    if (class->object.gcMask & ST_GC_MASK_ALIVE) {
        ST_Pool_scan(context, &class->instancePool, ST_GC_sweepVisitInstance);
    }
}

void ST_GC_sweep(ST_Internal_Context *context) {
    ST_Pool_scan(context, &context->classPool, ST_GC_sweepVisitClass);
}

/*//////////////////////////////////////////////////////////////////////////////
// Language types and methods
/////////////////////////////////////////////////////////////////////////////*/

static ST_Object ST_new(ST_Context context, ST_Object self, ST_Object argv[]) {
    ST_Class *class = self;
    return ST_Class_makeInstance(context, class);
}

static ST_Object ST_subclass(ST_Context context, ST_Object self,
                             ST_Object argv[]) {
    return ST_Class_subclass(context, self, 0, 0, 4);
}

static ST_Object ST_subclassExtended(ST_Context context, ST_Object self,
                                     ST_Object argv[]) {
    ST_Object rawgetSymb = ST_requestSymbol(context, "rawGet");
    ST_Object lenSymb = ST_requestSymbol(context, "length");
    ST_Object ivarNamesLength =
        ST_sendMessage(context, argv[1], lenSymb, 0, NULL);
    ST_Object cvarNamesLength =
        ST_sendMessage(context, argv[2], lenSymb, 0, NULL);
    ST_Integer_Rep ivarCount =
        (intptr_t)ST_sendMessage(context, ivarNamesLength, rawgetSymb, 0, NULL);
    ST_Integer_Rep cvarCount =
        (intptr_t)ST_sendMessage(context, cvarNamesLength, rawgetSymb, 0, NULL);
    return ST_Class_subclass(context, self, ivarCount, cvarCount, 4);
}

static ST_Object ST_class(ST_Context context, ST_Object self,
                          ST_Object argv[]) {
    return ((ST_Internal_Object *)self)->class;
}

static ST_Object ST_doesNotUnderstand(ST_Context context, ST_Object self,
                                      ST_Object argv[]) {
    return ST_getNilValue(context);
}

static void
ST_Internal_Context_initErrorHandling(ST_Internal_Context *context) {
    ST_SUBCLASS(context, "Object", "MessageNotUnderstood");
    ST_SETMETHOD(context, "Object", "doesNotUnderstand:", ST_doesNotUnderstand,
                 1);
    ST_SUBCLASS(context, "Object", "Message");
}

static bool ST_Internal_Context_bootstrap(ST_Internal_Context *context) {
    /* We need to do things manually for a bit, until we've defined the
 symbol class and the new method, because most of the functions in
 the runtime depend on Symbol. */
    ST_Class *cObject = ST_Pool_alloc(context, &context->classPool);
    ST_Class *cSymbol;
    ST_Internal_Object *symbolSymbol;
    ST_Internal_Object *newSymbol;
    ST_StringMap_Entry *newEntry =
        ST_Pool_alloc(context, &context->strmapNodePool);
    ST_BST_Node_init((ST_BST_Node *)newEntry);
    cObject->object.class = cObject;
    cObject->super = cObject;
    cObject->methodTree = NULL;
    cObject->finalizer = NULL;
    cObject->instanceVariableCount = 0;
    ST_Pool_init(context, &cObject->instancePool, sizeof(ST_Object), 10);
    cSymbol = ST_Class_subclass(context, cObject, 0, 0, 512);
    symbolSymbol = ST_Class_makeInstance(context, cSymbol);
    newSymbol = ST_Class_makeInstance(context, cSymbol);
    context->symbolRegistry = ST_Pool_alloc(context, &context->strmapNodePool);
    ST_BST_Node_init((ST_BST_Node *)context->symbolRegistry);
    context->symbolRegistry->key = ST_strdup(context, "Symbol");
    context->symbolRegistry->value = symbolSymbol;
    context->globalScope = ST_Pool_alloc(context, &context->gvarNodePool);
    ST_BST_Node_init((ST_BST_Node *)context->globalScope);
    context->globalScope->header.symbol = symbolSymbol;
    context->globalScope->value = (ST_Object)cSymbol;
    newEntry->key = ST_strdup(context, "new");
    newEntry->value = newSymbol;
    ST_BST_insert((ST_BST_Node **)&context->symbolRegistry,
                  &newEntry->nodeHeader, ST_StringMap_comparator);
    ST_setMethod(context, cObject, newSymbol, ST_new, 0);
    ST_setGlobal(context, ST_requestSymbol(context, "Object"), cObject);
    return true;
}

static ST_Object ST_ifTrueImplForTrue(ST_Context context, ST_Object self,
                                      ST_Object argv[]) {
    ST_Object valueSymbol = ST_requestSymbol(context, "value");
    return ST_sendMessage(context, argv[0], valueSymbol, 0, NULL);
}

static ST_Object ST_ifFalseImplForFalse(ST_Context context, ST_Object self,
                                        ST_Object argv[]) {
    ST_Object valueSymbol = ST_requestSymbol(context, "value");
    return ST_sendMessage(context, argv[0], valueSymbol, 0, NULL);
}

static ST_Object ST_nopMethod(ST_Context context, ST_Object self,
                              ST_Object argv[]) {
    return ST_getNilValue(context);
}

static void ST_initNil(ST_Internal_Context *context) {
    ST_SUBCLASS(context, "Object", "UndefinedObject");
    context->nilValue = ST_NEW(context, "UndefinedObject");
}

static void ST_initBoolean(ST_Internal_Context *context) {
    ST_SUBCLASS(context, "Object", "Boolean");
    ST_SUBCLASS(context, "Boolean", "True");
    ST_SUBCLASS(context, "Boolean", "False");
    ST_SETMETHOD(context, "True", "ifTrue:", ST_ifTrueImplForTrue, 1);
    ST_SETMETHOD(context, "True", "ifFalse:", ST_nopMethod, 1);
    ST_SETMETHOD(context, "False", "ifFalse:", ST_ifFalseImplForFalse, 1);
    ST_SETMETHOD(context, "False", "ifTrue:", ST_nopMethod, 1);
    context->trueValue = ST_NEW(context, "True");
    context->falseValue = ST_NEW(context, "False");
}

ST_Context ST_createContext(const ST_Context_Configuration *config) {
    ST_Internal_Context *ctx =
        config->memory.allocFn(sizeof(ST_Internal_Context));
    if (!ctx)
        return NULL;
    ctx->config = *config;
    ST_Pool_init(ctx, &ctx->gvarNodePool, sizeof(ST_GlobalVarMap_Entry), 1024);
    ST_Pool_init(ctx, &ctx->vmFramePool, sizeof(ST_VM_Frame), 50);
    ST_Pool_init(ctx, &ctx->methodNodePool, sizeof(ST_MethodMap_Entry), 1024);
    ST_Pool_init(ctx, &ctx->strmapNodePool, sizeof(ST_StringMap_Entry), 1024);
    ST_Pool_init(ctx, &ctx->classPool, sizeof(ST_Class), 100);
    ST_Internal_Context_bootstrap(ctx);
    ctx->operandStack.base = ST_alloc(ctx, sizeof(ST_Internal_Object *) *
                                               config->memory.stackCapacity);
    ctx->operandStack.top =
        (struct ST_Internal_Object **)ctx->operandStack.base;
    ST_SETMETHOD(ctx, "Object", "subclass", ST_subclass, 0);
    ST_SETMETHOD(ctx, "Object",
                 "subclass:instanceVariableNames:classVariableNames:",
                 ST_subclassExtended, 3);
    ST_SETMETHOD(ctx, "Object", "class", ST_class, 0);
    ST_initNil(ctx);
    ST_initBoolean(ctx);
    ST_Internal_Context_initErrorHandling(ctx);
    ST_initInteger(ctx);
    ST_initArray(ctx);
    return ctx;
}

void ST_destroyContext(const ST_Context context) {
    ST_Internal_Context *ctxImpl = context;
    /* TODO: lots of stuff */
    ST_free(context, ctxImpl->operandStack.base);
    ST_Pool_release(context, &ctxImpl->gvarNodePool);
    ST_Pool_release(context, &ctxImpl->vmFramePool);
    ST_Pool_release(context, &ctxImpl->methodNodePool);
    ST_Pool_release(context, &ctxImpl->strmapNodePool);
    ST_Pool_release(context, &ctxImpl->classPool);
}
