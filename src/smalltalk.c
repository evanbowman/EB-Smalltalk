#include "smalltalk.h"

struct ST_Context;
struct ST_Internal_Object;

/* Note: stack operations are not currently bounds-checked. But I'm thinking
   there's a more efficient way of preventing bad access than checking every
   call. e.g. A compiler would know how many local variables are in a method,
   so lets eventually make the vm do the stack size check upon entry into a
   method. */
static void ST_pushStack(struct ST_Context *ctx, ST_Object val);
static void ST_popStack(struct ST_Context *ctx);
static ST_Object ST_refStack(struct ST_Context *ctx, ST_Size offset);

static ST_Size ST_stackSize(struct ST_Context *ctx);

/* Note: For the most part, only ST_Pool_alloc/free should call ST_alloc or
   ST_free directly. */
static void *ST_alloc(ST_Object ctx, ST_Size size);
static void ST_memset(ST_Object ctx, void *memory, int val, ST_Size n);
static void ST_memcpy(ST_Object ctx, void *dest, const void *src, ST_Size n);
static void ST_free(ST_Object ctx, void *memory);

static struct ST_Internal_Object *ST_GC_allocObject(struct ST_Context *ctx,
                                                    ST_Size size);

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

ST_Cmp ST_strcmp(char string1[], char string2[]) {
    int i;
    for (i = 0;; i++) {
        if (string1[i] != string2[i]) {
            return string1[i] < string2[i] ? ST_Cmp_Less : ST_Cmp_Greater;
        }
        if (string1[i] == '\0') {
            return ST_Cmp_Eq;
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

static char *ST_strdup(ST_Object ctx, const char *s) {
    char *d = ST_alloc(ctx, ST_strlen(s) + 1);
    if (d == NULL)
        return NULL;
    ST_strcpy(d, s);
    return d;
}

typedef struct ST_Visitor {
    void (*visit)(struct ST_Visitor *, void *);
} ST_Visitor;

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

static void ST_Pool_grow(ST_Object ctx, ST_Pool *pool, ST_Size count) {
    const ST_Size poolNodeSize = pool->elemSize + sizeof(ST_Pool_Node);
    const ST_Size headerSize = sizeof(ST_Pool_Slab *);
    const ST_Size allocSize = poolNodeSize * count + headerSize;
    ST_U8 *mem = ST_alloc(ctx, allocSize);
    ST_Size i;
    ST_memset(ctx, mem, 0, allocSize);
    pool->lastAllocCount = count;
    for (i = headerSize; i < allocSize; i += poolNodeSize) {
        ST_Pool_Node *node = (void *)(mem + i);
        node->next = pool->freelist;
        pool->freelist = node;
    }
    ((ST_Pool_Slab *)mem)->next = pool->slabs;
    pool->slabs = (ST_Pool_Slab *)mem;
}

static void ST_Pool_init(ST_Object ctx, ST_Pool *pool, ST_Size elemSize,
                         ST_Size count) {
    pool->freelist = NULL;
    pool->slabs = NULL;
    pool->elemSize = elemSize;
    ST_Pool_grow(ctx, pool, count);
}

enum { ST_POOL_GROWTH_FACTOR = 2 };

static void *ST_Pool_alloc(ST_Object ctx, ST_Pool *pool) {
    ST_Pool_Node *ret;
    if (!pool->freelist) {
        ST_Pool_grow(ctx, pool, pool->lastAllocCount * ST_POOL_GROWTH_FACTOR);
    }
    ret = pool->freelist;
    pool->freelist = pool->freelist->next;
    /* Note: advancing the node pointer by the size of the node effectively
 strips the header, thus setting the pointer to the contained element.
 see comment in ST_Pool_Node. */
    return (ST_U8 *)ret + sizeof(ST_Pool_Node);
}

static void ST_Pool_free(ST_Object ctx, ST_Pool *pool, void *mem) {
    ST_Pool_Node *node = (void *)((char *)mem - sizeof(ST_Pool_Node));
    node->next = pool->freelist;
    pool->freelist = node;
}

static void ST_Pool_release(ST_Object ctx, ST_Pool *pool) {
    ST_Pool_Slab *slab = pool->slabs;
    while (slab) {
        ST_Pool_Slab *next = slab->next;
        ST_free(ctx, slab);
        slab = next;
    }
}

/*//////////////////////////////////////////////////////////////////////////////
// Object struct
/////////////////////////////////////////////////////////////////////////////*/

enum ST_GC_Mask {
    ST_GC_MASK_ALIVE = 1,
    ST_GC_MASK_MARKED = 1 << 1,
    ST_GC_MASK_PRESERVE = 1 << 2
};

typedef struct ST_Internal_Object {
    struct ST_Class *class;
    ST_U8 gcMask;
    /* Note:
   Unless an object is a class, there's an instance variable array inlined
   at the end of the struct.
   struct ST_Internal_Object *InstanceVariables[] */
} ST_Internal_Object;

/*//////////////////////////////////////////////////////////////////////////////
// Context struct
/////////////////////////////////////////////////////////////////////////////*/

typedef struct ST_ContextObject { ST_Internal_Object object; } ST_ContextObject;

typedef struct ST_StackFrame {
    ST_Size ip;
    ST_Size bp;
    const ST_Code *code;
    struct ST_StackFrame *parent;
} ST_StackFrame;

typedef struct ST_Context {
    ST_ContextObject object;
    ST_Configuration config;
    struct ST_StringMap_Entry *symbolRegistry;
    struct ST_GlobalVarMap_Entry *globalScope;
    struct ST_Internal_Object *nilValue;
    struct ST_Internal_Object *trueValue;
    struct ST_Internal_Object *falseValue;
    ST_StackFrame *stackFrame;
    struct OperandStack {
        const struct ST_Internal_Object **base;
        struct ST_Internal_Object **top;
    } operandStack;
    struct Heap {
        ST_U8 *begin;
        ST_U8 *end;
    } heap;
    ST_Pool gvarNodePool;
    ST_Pool vmFramePool;
    ST_Pool methodNodePool;
    ST_Pool strmapNodePool;
    ST_Pool classPool;
    ST_Pool integerPool;
    bool gcDisabled;
} ST_Context;

static void ST_pushStackFrame(ST_Context *ctx, ST_Size ip,
                              const ST_Code *code) {
    ST_StackFrame *newFrame = ST_Pool_alloc(ctx, &ctx->vmFramePool);
    newFrame->ip = ip;
    newFrame->code = code;
    newFrame->bp = ST_stackSize(ctx);
    newFrame->parent = ctx->stackFrame;
    ctx->stackFrame = newFrame;
}

static void ST_popStackFrame(ST_Context *ctx) {
    ST_StackFrame *completeFrame = ctx->stackFrame;
    ST_Size i;
    const ST_Size stackDiff = ST_stackSize(ctx) - completeFrame->bp;
    if (completeFrame->bp < completeFrame->parent->bp) {
        /* FIXME */
    }
    for (i = 0; i < stackDiff; ++i) {
        ST_popStack(ctx);
    }
    completeFrame = completeFrame->parent;
    ST_Pool_free(ctx, &ctx->vmFramePool, completeFrame);
}

ST_Object *ST_pushLocals(ST_Object ctx, ST_Size count) {
    ST_Size i;
    ST_pushStackFrame(ctx, 0, NULL);
    for (i = 0; i < count; ++i) {
        ST_pushStack(ctx, ST_getNil(ctx));
    }
    return (ST_Object *)((ST_Context *)ctx)->operandStack.top;
}

void ST_popLocals(ST_Object ctx) { ST_popStackFrame(ctx); }

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

static ST_BST_Node *ST_BST_remove(ST_BST_Node **root, void *key,
                                  ST_Cmp (*comp)(void *, void *)) {
    ST_BST_Node *current = *root;
    ST_BST_Node **backlink = root;
    if (UNEXPECTED(!current)) {
        return NULL;
    }
    while (true) {
        switch (comp(current, key)) {
        case ST_Cmp_Eq:
            if (!current->left && !current->right) {
                *backlink = NULL;
            } else if (current->left && !current->right) {
                *backlink = current->left;
            } else if (!current->left && current->right) {
                *backlink = current->right;
            } else {
                ST_BST_Node **maxBacklink = &current->left;
                ST_BST_Node *max = current->left;
                ST_BST_Node temp;
                while (max->right) {
                    maxBacklink = &max->right;
                    max = max->right;
                }
                temp = *max;
                *max = *current;
                *backlink = max;
                if (maxBacklink == &current->left) {
                    maxBacklink = &max->left;
                }
                *maxBacklink = temp.left;
            }
            return current;
        case ST_Cmp_Greater:
            if (current->left) {
                backlink = &current->left;
                current = current->left;
                break;
            } else
                return NULL;
        case ST_Cmp_Less:
            if (current->right) {
                backlink = &current->right;
                current = current->right;
                break;
            } else
                return NULL;
        }
    }
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

static void ST_BST_traverse(ST_BST_Node *root, ST_Visitor *visitor) {
    ST_BST_Node *current, *parent;
    if (root == NULL)
        return;
    current = root;
    while (current != NULL) {
        if (current->left == NULL) {
            visitor->visit(visitor, current);
            current = current->right;
        } else {
            parent = current->left;
            while (parent->right != NULL && parent->right != current)
                parent = parent->right;

            if (parent->right == NULL) {
                parent->right = current;
                current = current->left;
            } else {
                parent->right = NULL;
                visitor->visit(visitor, current);
                current = current->right;
            }
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

static ST_Size ST_getObjectFootprint(ST_Size ivarCount) {
    return ivarCount * sizeof(ST_Object) + sizeof(ST_Internal_Object);
}

static void ST_Object_setGCMask(ST_Object obj, enum ST_GC_Mask mask) {
    ((ST_Internal_Object *)obj)->gcMask |= mask;
}

/* static void ST_Object_unsetGCMask(ST_Object obj, enum ST_GC_Mask mask) { */
/*     ((ST_Internal_Object *)obj)->gcMask &= ~mask; */
/* } */

/* static bool ST_Object_matchGCMask(ST_Object obj, enum ST_GC_Mask mask) { */
/*     return ((ST_Internal_Object *)obj)->gcMask & mask; */
/* } */

static ST_Internal_Object **ST_Object_getIVars(ST_Internal_Object *object) {
    return (void *)((ST_U8 *)object + sizeof(ST_Internal_Object));
}

ST_Object ST_getClass(ST_Object ctx, ST_Object object) {
    return ((ST_Internal_Object *)object)->class;
}

typedef struct ST_Class {
    ST_Internal_Object object;
    ST_MethodMap_Entry *methodTree;
    struct ST_Class *super;
    ST_U16 instanceVariableCount;
    /* Note: while in most cases we could figure out instance size from the
       number of ivars, for some special cases, e.g. builtin integers, objects
       contain extra memory that isn't meant to be an explorable gc root. */
    ST_Size instanceSize;
    ST_Object *instanceVariableNames;
    ST_Object name;
} ST_Class;

ST_Object ST_getSuper(ST_Object ctx, ST_Object object) {
    return ((ST_Internal_Object *)object)->class->super;
}

ST_Internal_Object *ST_Class_makeInstance(ST_Context *ctx, ST_Class *class) {
    ST_Internal_Object *instance = ST_GC_allocObject(ctx, class->instanceSize);
    ST_Internal_Object **ivars = ST_Object_getIVars(instance);
    ST_Size i;
    for (i = 0; i < class->instanceVariableCount; ++i) {
        ivars[i] = ST_getNil(ctx);
    }
    instance->class = class;
    ST_Object_setGCMask(instance, ST_GC_MASK_ALIVE);
    return instance;
}

static ST_Class *ST_Class_subclass(ST_Context *ctx, ST_Class *super,
                                   ST_Object nameSymb,
                                   ST_Size instanceVariableCount,
                                   ST_Size classVariableCount) {
    ST_Class *sub = ST_Pool_alloc(ctx, &((ST_Context *)ctx)->classPool);
    sub->object.class = sub;
    ST_Object_setGCMask(sub, ST_GC_MASK_ALIVE);
    sub->super = super;
    sub->instanceVariableCount =
        ((ST_Class *)super)->instanceVariableCount + instanceVariableCount;
    sub->instanceSize = ST_getObjectFootprint(sub->instanceVariableCount);
    if (instanceVariableCount) {
        sub->instanceVariableNames =
            ST_alloc(ctx, instanceVariableCount * sizeof(ST_Internal_Object *));
    } else {
        sub->instanceVariableNames = NULL;
    }
    sub->name = nameSymb;
    sub->methodTree = NULL;
    return sub;
}

static bool ST_isClass(ST_Internal_Object *object) {
    return (ST_Class *)object == object->class;
}

static ST_Internal_Method *
ST_Internal_Object_getMethod(ST_Object ctx, ST_Internal_Object *obj,
                             ST_Internal_Object *symbol) {
    ST_Class *currentClass = obj->class;
    while (true) {
        ST_SymbolMap_Entry searchTmpl;
        ST_BST_Node *found;
        searchTmpl.symbol = symbol;
        found = ST_BST_find((ST_BST_Node **)&currentClass->methodTree,
                            &searchTmpl, ST_SymbolMap_comparator);
        if (found) {
            return &((ST_MethodMap_Entry *)found)->method;
        } else {
            if (currentClass->super) {
                currentClass = currentClass->super;
            } else {
                return NULL;
            }
        }
    }
}

static void ST_failedMethodLookup(ST_Object ctx, ST_Object receiver,
                                  ST_Object symbol) {
    ST_Object cMNU = ST_getGlobal(ctx, ST_symb(ctx, "MessageNotUnderstood"));
    ST_Object err = ST_sendMsg(ctx, cMNU, ST_symb(ctx, "new"), 0, NULL);
    ST_sendMsg(ctx, receiver, ST_symb(ctx, "doesNotUnderstand:"), 1, &err);
}

ST_Object ST_sendMsg(ST_Object ctx, ST_Object receiver, ST_Object symbol,
                     ST_U8 argc, ST_Object argv[]) {
    ST_Internal_Method *method =
        ST_Internal_Object_getMethod(ctx, receiver, symbol);
    if (method) {
        switch (method->type) {
        case ST_METHOD_TYPE_PRIMITIVE:
            if (argc != method->argc) {
                /* FIXME: wrong number of args */
                return ST_getNil(ctx);
            }
            return method->payload.primitiveMethod(ctx, receiver, argv);

        case ST_METHOD_TYPE_COMPILED: {
            ST_U8 i;
            ST_Object result;
            for (i = 0; i < argc; ++i) {
                ST_pushStack(ctx, argv[i]);
            }
            ST_VM_execute(ctx, method->payload.compiledMethod.source,
                          method->payload.compiledMethod.offset);
            result = ST_refStack(ctx, 0);
            ST_popStack(ctx);            /* Pop result */
            for (i = 0; i < argc; ++i) { /* Need to pop argv too */
                ST_popStack(ctx);
            }
            return result;
        }
        }
    }
    ST_failedMethodLookup(ctx, receiver, symbol);
    return ST_getNil(ctx);
}

static bool ST_Class_insertMethodEntry(ST_Object ctx, ST_Class *class,
                                       ST_MethodMap_Entry *entry) {
    if (!ST_BST_insert((ST_BST_Node **)&((ST_Class *)class)->methodTree,
                       &entry->header.node, ST_SymbolMap_comparator)) {
        return false;
    }
    ST_BST_splay((ST_BST_Node **)&((ST_Class *)class)->methodTree,
                 &entry->header.node, ST_SymbolMap_comparator);
    return true;
}

void ST_setMethod(ST_Object ctx, ST_Object object, ST_Object symbol,
                  ST_PrimitiveMethod primitiveMethod, ST_U8 argc) {
    ST_Pool *methodPool = &((ST_Context *)ctx)->methodNodePool;
    ST_MethodMap_Entry *entry = ST_Pool_alloc(ctx, methodPool);
    ST_BST_Node_init((ST_BST_Node *)entry);
    entry->header.symbol = symbol;
    entry->method.type = ST_METHOD_TYPE_PRIMITIVE;
    entry->method.payload.primitiveMethod = primitiveMethod;
    entry->method.argc = argc;
    if (!ST_Class_insertMethodEntry(ctx, ((ST_Internal_Object *)object)->class,
                                    entry)) {
        ST_Pool_free(ctx, methodPool, entry);
    }
}

/*//////////////////////////////////////////////////////////////////////////////
// Context
/////////////////////////////////////////////////////////////////////////////*/

static void *ST_alloc(ST_Object ctx, ST_Size size) {
    void *mem = ((ST_Context *)ctx)->config.memory.allocFn(size);
    if (UNEXPECTED(!mem)) {
        /* FIXME! */
    }
    return mem;
}

static void ST_free(ST_Object ctx, void *memory) {
    ((ST_Context *)ctx)->config.memory.freeFn(memory);
}

static void ST_memcpy(ST_Object ctx, void *dest, const void *src, ST_Size n) {
    ((ST_Context *)ctx)->config.memory.copyFn(dest, src, n);
}

static void ST_memset(ST_Object ctx, void *memory, int val, ST_Size n) {
    ((ST_Context *)ctx)->config.memory.setFn(memory, val, n);
}

typedef struct ST_StringMap_Entry {
    ST_BST_Node nodeHeader;
    char *key;
    void *value;
} ST_StringMap_Entry;

static ST_Cmp ST_StringMap_comparator(void *left, void *right) {
    ST_StringMap_Entry *lhsEntry = left;
    ST_StringMap_Entry *rhsEntry = right;
    return ST_strcmp(lhsEntry->key, rhsEntry->key);
}

typedef struct ST_GlobalVarMap_Entry {
    ST_SymbolMap_Entry header;
    ST_Internal_Object *value;
} ST_GlobalVarMap_Entry;

static void ST_pushStack(ST_Context *ctx, ST_Object val) {
    *(ctx->operandStack.top++) = val;
}

static void ST_popStack(struct ST_Context *ctx) { --ctx->operandStack.top; }

static ST_Object ST_refStack(struct ST_Context *ctx, ST_Size offset) {
    return *(ctx->operandStack.top - (offset + 1));
}

static ST_Size ST_stackSize(struct ST_Context *ctx) {
    return (const ST_Internal_Object **)ctx->operandStack.top -
           ctx->operandStack.base;
}

ST_Object ST_getGlobal(ST_Object ctx, ST_Object symbol) {
    ST_BST_Node *found;
    ST_BST_Node **globalScope = (void *)&((ST_Context *)ctx)->globalScope;
    ST_SymbolMap_Entry searchTmpl;
    searchTmpl.symbol = symbol;
    found = ST_BST_find(globalScope, &searchTmpl, ST_SymbolMap_comparator);
    if (UNEXPECTED(!found)) {
        return ST_getNil(ctx);
    }
    ST_BST_splay(globalScope, &symbol, ST_SymbolMap_comparator);
    return ((ST_GlobalVarMap_Entry *)found)->value;
}

void ST_setGlobal(ST_Object ctx, ST_Object symbol, ST_Object object) {
    ST_Context *ctxImpl = ctx;
    ST_SymbolMap_Entry searchTmpl;
    ST_BST_Node *found;
    searchTmpl.symbol = symbol;
    if (UNEXPECTED(object == ST_getNil(ctx))) {
        ST_BST_Node *removed =
            ST_BST_remove((ST_BST_Node **)&ctxImpl->globalScope, &searchTmpl,
                          ST_SymbolMap_comparator);
        ST_Pool_free(ctx, &ctxImpl->gvarNodePool, removed);
    } else if ((found = ST_BST_find((ST_BST_Node **)&ctxImpl->globalScope,
                                    &searchTmpl, ST_SymbolMap_comparator))) {
        ((ST_GlobalVarMap_Entry *)found)->value = object;
    } else {
        ST_GlobalVarMap_Entry *entry =
            ST_Pool_alloc(ctxImpl, &ctxImpl->gvarNodePool);
        ST_BST_Node_init((ST_BST_Node *)entry);
        entry->header.symbol = symbol;
        entry->value = object;
        if (!ST_BST_insert((ST_BST_Node **)&ctxImpl->globalScope,
                           &entry->header.node, ST_SymbolMap_comparator)) {
            ST_Pool_free(ctx, &ctxImpl->gvarNodePool, entry);
        }
    }
}

ST_Object ST_getNil(ST_Object ctx) { return ((ST_Context *)ctx)->nilValue; }

ST_Object ST_getTrue(ST_Object ctx) { return ((ST_Context *)ctx)->trueValue; }

ST_Object ST_getFalse(ST_Object ctx) { return ((ST_Context *)ctx)->falseValue; }

ST_Object ST_symb(ST_Object ctx, const char *symbolName) {
    ST_Context *extCtx = ctx;
    ST_BST_Node *found;
    ST_StringMap_Entry searchTmpl;
    ST_StringMap_Entry *newEntry;
    ST_Internal_Object *newSymb;
    searchTmpl.key = (char *)symbolName;
    found = ST_BST_find((ST_BST_Node **)&extCtx->symbolRegistry, &searchTmpl,
                        ST_StringMap_comparator);
    if (found) {
        return (ST_Object)((ST_StringMap_Entry *)found)->value;
    }
    newEntry = ST_Pool_alloc(ctx, &extCtx->strmapNodePool);
    ST_BST_Node_init((ST_BST_Node *)newEntry);
    newEntry->key = ST_strdup(ctx, symbolName);
    newSymb = ST_sendMsg(ctx, ST_getGlobal(ctx, ST_symb(ctx, "Symbol")),
                         ST_symb(ctx, "new"), 0, NULL);
    ST_Object_setGCMask(newSymb, ST_GC_MASK_PRESERVE);
    newEntry->value = newSymb;
    if (!ST_BST_insert((ST_BST_Node **)&extCtx->symbolRegistry,
                       &newEntry->nodeHeader, ST_StringMap_comparator)) {
        ST_free(ctx, newEntry->key);
        ST_Pool_free(ctx, &extCtx->strmapNodePool, newEntry);
        return ST_getNil(ctx);
    }
    return newEntry->value;
}

typedef struct ST_SymbolNameByValueVisitor {
    ST_Visitor visitor;
    ST_Object key;
    const char *result;
} ST_SymbolNameByValueVisitor;

static void ST_findSymbolNameByValue(ST_Visitor *visitor, void *mapNode) {
    ST_StringMap_Entry *nodeImpl = (ST_StringMap_Entry *)mapNode;
    ST_SymbolNameByValueVisitor *visitorImpl =
        (ST_SymbolNameByValueVisitor *)visitor;
    if (visitorImpl->key == nodeImpl->value) {
        visitorImpl->result = nodeImpl->key;
    }
}

const char *ST_Symbol_toString(ST_Object ctx, ST_Object symbol) {
    ST_SymbolNameByValueVisitor visitor;
    visitor.visitor.visit = ST_findSymbolNameByValue;
    visitor.key = symbol;
    visitor.result = NULL;
    ST_BST_traverse((ST_BST_Node *)((ST_Context *)ctx)->symbolRegistry,
                    (ST_Visitor *)&visitor);
    return visitor.result;
}

/*//////////////////////////////////////////////////////////////////////////////
// VM
/////////////////////////////////////////////////////////////////////////////*/

#include "opcode.h"

static void ST_VM_invokePrimitiveMethod_NArg(ST_Context *ctx,
                                             ST_Object receiver,
                                             ST_Internal_Method *method) {
    ST_Object argv[UINT8_MAX];
    ST_U8 i;
    for (i = 0; i < method->argc; ++i) {
        argv[i] = ST_refStack(ctx, 0);
        ST_popStack(ctx);
    }
    ST_pushStack(ctx, method->payload.primitiveMethod(ctx, receiver, argv));
}

/* Note: ST_readLE[n] used to do a byteswap, but it happens at load time now. */
static ST_U16 ST_readLE16(ST_StackFrame *f) {
    const ST_U16 rt = *(ST_U16 *)(f->code->instructions + f->ip);
    f->ip += sizeof(ST_U16);
    return rt;
}

static ST_U32 ST_readLE32(ST_StackFrame *f) {
    const ST_U32 rt = *(ST_U32 *)(f->code->instructions + f->ip);
    f->ip += sizeof(ST_U32);
    return rt;
}

static void ST_Internal_VM_execute(ST_Context *ctx) {
    while (ctx->stackFrame->ip < ctx->stackFrame->code->length) {
        switch (ctx->stackFrame->code->instructions[ctx->stackFrame->ip++]) {
        case ST_VM_OP_PUSHNIL:
            ST_pushStack(ctx, ST_getNil(ctx));
            break;

        case ST_VM_OP_PUSHTRUE:
            ST_pushStack(ctx, ST_getTrue(ctx));
            break;

        case ST_VM_OP_PUSHFALSE:
            ST_pushStack(ctx, ST_getFalse(ctx));
            break;

        case ST_VM_OP_PUSHSUPER: {
            ST_Internal_Object *obj = ST_refStack(ctx, 0);
            ST_popStack(ctx);
            ST_pushStack(ctx, obj->class->super);
        } break;

        case ST_VM_OP_DUP: {
            ST_pushStack(ctx, ST_refStack(ctx, 0));
        } break;

        case ST_VM_OP_POP: {
            ST_popStack(ctx);
            break;
        }

        case ST_VM_OP_RETURN: {
            ST_Object ret = ST_refStack(ctx, 0);
            ST_popStackFrame(ctx);
            ST_pushStack(ctx, ret);
            /* NOTE: we jumped frames--reverting back to the instruction pointer
               before the call--which is why we don't increment frame->ip. */
        } break;

        case ST_VM_OP_GETGLOBAL: {
            ST_Object gVarSymb =
                ctx->stackFrame->code->symbTab[ST_readLE16(ctx->stackFrame)];
            ST_Object global = ST_getGlobal(ctx, gVarSymb);
            ST_pushStack(ctx, global);
        } break;

        case ST_VM_OP_SETGLOBAL: {
            ST_Object gVarSymb =
                ctx->stackFrame->code->symbTab[ST_readLE16(ctx->stackFrame)];
            ST_setGlobal(ctx, gVarSymb, ST_refStack(ctx, 0));
            ST_popStack(ctx);
        } break;

        case ST_VM_OP_GETIVAR: {
            ST_U16 ivarIndex = ST_readLE16(ctx->stackFrame);
            ST_Object target = ST_refStack(ctx, 0);
            ST_popStack(ctx);
            ST_pushStack(ctx, ST_Object_getIVars(target)[ivarIndex]);
        } break;

        case ST_VM_OP_SETIVAR: {
            ST_U16 ivarIndex = ST_readLE16(ctx->stackFrame);
            ST_Object target = ST_refStack(ctx, 0);
            ST_Object value = ST_refStack(ctx, 1);
            ST_popStack(ctx);
            ST_popStack(ctx);
            ST_Object_getIVars(target)[ivarIndex] = value;
        } break;

        case ST_VM_OP_SENDMSG: {
            const ST_Object symbol =
                ctx->stackFrame->code->symbTab[ST_readLE16(ctx->stackFrame)];
            ST_Object receiver = ST_refStack(ctx, 0);
            ST_Internal_Method *method;
            method = ST_Internal_Object_getMethod(ctx, receiver, symbol);
            if (method) {
                switch (method->type) {
                case ST_METHOD_TYPE_PRIMITIVE:
                    ST_popStack(ctx); /* pop receiver */
                    ST_VM_invokePrimitiveMethod_NArg(ctx, receiver, method);
                    break;

                case ST_METHOD_TYPE_COMPILED: {
                    ST_pushStackFrame(ctx,
                                      method->payload.compiledMethod.offset,
                                      method->payload.compiledMethod.source);
                } break;
                }
            } else {
                ST_failedMethodLookup(ctx, receiver, symbol);
            }
        } break;

        case ST_VM_OP_PUSHSYMBOL:
            ST_pushStack(
                ctx,
                ctx->stackFrame->code->symbTab[ST_readLE16(ctx->stackFrame)]);
            break;

        /* TODO: re-verify that this still works */
        case ST_VM_OP_SETMETHOD: {
            const ST_Object symbol =
                ctx->stackFrame->code->symbTab[ST_readLE16(ctx->stackFrame)];
            const ST_Object target = ST_refStack(ctx, 0);
            const ST_U8 argc =
                ctx->stackFrame->code->instructions[ctx->stackFrame->ip++];
            ST_MethodMap_Entry *entry =
                ST_Pool_alloc(ctx, &ctx->methodNodePool);
            ST_BST_Node_init((ST_BST_Node *)entry);
            entry->header.symbol = symbol;
            entry->method.type = ST_METHOD_TYPE_COMPILED;
            entry->method.argc = argc;
            entry->method.payload.compiledMethod.source = ctx->stackFrame->code;
            entry->method.payload.compiledMethod.offset =
                ctx->stackFrame->ip + sizeof(ST_U32) + 1;
            if (!ST_Class_insertMethodEntry(ctx, target, entry)) {
                ST_Pool_free(ctx, &ctx->methodNodePool, entry);
            }
            ST_popStack(ctx);
            ctx->stackFrame->ip += ST_readLE32(ctx->stackFrame);
        } break;

        default:
            return; /* FIXME */
        }
    }
}

void ST_VM_execute(ST_Object ctx, const ST_Code *code, ST_Size offset) {
    ST_pushStackFrame(ctx, offset, code);
    ST_Internal_VM_execute(ctx);
    /* FIXME: users should never call execute directly with the offset set
       to the beginning of a method. Under normal circumstances, */
}

/*//////////////////////////////////////////////////////////////////////////////
// Integer
/////////////////////////////////////////////////////////////////////////////*/

static const char *ST_subcMethodName = "subclass:";
static const char *ST_subcExtMethodName =
    "subclass:instanceVariableNames:classVariableNames:";

static ST_Object ST_nopMethod(ST_Object ctx, ST_Object self, ST_Object argv[]) {
    return ST_getNil(ctx);
}

typedef struct ST_Integer {
    ST_Internal_Object object;
    ST_S32 value;
} ST_Integer;

static bool ST_Integer_typecheck(ST_Internal_Object *lhs,
                                 ST_Internal_Object *rhs) {
    return lhs->class == rhs->class;
}

static ST_Object ST_Integer_add(ST_Object ctx, ST_Object self,
                                ST_Object argv[]) {
    ST_Integer *ret;
    if (!ST_Integer_typecheck(self, argv[0]))
        return ST_getNil(ctx);
    ret = (ST_Integer *)ST_Class_makeInstance(ctx, ST_getClass(ctx, self));
    ret->value = ((ST_Integer *)self)->value + ((ST_Integer *)argv[0])->value;
    return ret;
}

static ST_Object ST_Integer_sub(ST_Object ctx, ST_Object self,
                                ST_Object argv[]) {
    ST_Integer *ret;
    if (!ST_Integer_typecheck(self, argv[0]))
        return ST_getNil(ctx);
    ret = (ST_Integer *)ST_Class_makeInstance(ctx, ST_getClass(ctx, self));
    ret->value = ((ST_Integer *)self)->value - ((ST_Integer *)argv[0])->value;
    return ret;
}

static ST_Object ST_Integer_mul(ST_Object ctx, ST_Object self,
                                ST_Object argv[]) {
    ST_Integer *ret;
    if (!ST_Integer_typecheck(self, argv[0]))
        return ST_getNil(ctx);
    ret = (ST_Integer *)ST_Class_makeInstance(ctx, ST_getClass(ctx, self));
    ret->value = ((ST_Integer *)self)->value * ((ST_Integer *)argv[0])->value;
    return ret;
}

static ST_Object ST_Integer_div(ST_Object ctx, ST_Object self,
                                ST_Object argv[]) {
    ST_Integer *ret;
    if (!ST_Integer_typecheck(self, argv[0]))
        return ST_getNil(ctx);
    ret = (ST_Integer *)ST_Class_makeInstance(ctx, ST_getClass(ctx, self));
    ret->value = ((ST_Integer *)self)->value / ((ST_Integer *)argv[0])->value;
    return ret;
}

static ST_Object ST_Integer_rawGet(ST_Object ctx, ST_Object self,
                                   ST_Object argv[]) {
    return (ST_Object)(intptr_t)((ST_Integer *)self)->value;
}

static ST_Object ST_Integer_rawSet(ST_Object ctx, ST_Object self,
                                   ST_Object argv[]) {
    ((ST_Integer *)self)->value = (ST_S32)(intptr_t)argv[0];
    return ST_getNil(ctx);
}

static void ST_initInteger(ST_Context *ctx) {
    ST_Class *cObj = ST_getGlobal(ctx, ST_symb(ctx, "Object"));
    ST_Class *cInt = ST_Pool_alloc(ctx, &ctx->classPool);
    ST_Object intSymb = ST_symb(ctx, "Integer");
    cInt->instanceVariableCount = 0;
    cInt->instanceVariableNames = NULL;
    cInt->instanceSize = sizeof(ST_Integer);
    cInt->methodTree = NULL;
    cInt->super = cObj;
    cInt->object.class = cInt;
    cInt->name = intSymb;
    ST_Object_setGCMask(cInt, ST_GC_MASK_ALIVE);
    ST_setMethod(ctx, cInt, ST_symb(ctx, "+"), ST_Integer_add, 1);
    ST_setMethod(ctx, cInt, ST_symb(ctx, "-"), ST_Integer_sub, 1);
    ST_setMethod(ctx, cInt, ST_symb(ctx, "*"), ST_Integer_mul, 1);
    ST_setMethod(ctx, cInt, ST_symb(ctx, "/"), ST_Integer_div, 1);
    ST_setMethod(ctx, cInt, ST_symb(ctx, "rawSet:"), ST_Integer_rawSet, 1);
    ST_setMethod(ctx, cInt, ST_symb(ctx, "rawGet"), ST_Integer_rawGet, 0);
    ST_setMethod(ctx, cInt, ST_symb(ctx, ST_subcMethodName), ST_nopMethod, 1);
    ST_setMethod(ctx, cInt, ST_symb(ctx, ST_subcExtMethodName), ST_nopMethod,
                 3);
    ST_setGlobal(ctx, intSymb, cInt);
}

/*//////////////////////////////////////////////////////////////////////////////
// Array
/////////////////////////////////////////////////////////////////////////////*/

static ST_Object ST_Array_new(ST_Object ctx, ST_Object self, ST_Object argv[]) {
    ST_Object result = ST_getNil(ctx);
    ST_Object rgetSymb = ST_symb(ctx, "rawGet");
    ST_Object lengthParam = argv[0];
    ST_S32 lengthValue =
        (intptr_t)ST_sendMsg(ctx, lengthParam, rgetSymb, 0, NULL);
    ST_Object newSymb = ST_symb(ctx, "new");
    ST_S32 i;
    enum { LOC_arr, LOC_cLNode, LOC_list, LOC_nodeInst, LOC_count };
    ST_Object *locals = ST_pushLocals(ctx, LOC_count);
    ST_Internal_Object **ivars;
    locals[LOC_arr] = ST_Class_makeInstance(ctx, self);
    locals[LOC_cLNode] = ST_getGlobal(ctx, ST_symb(ctx, "ListNode"));
    locals[LOC_list] = ST_getNil(ctx);
    ivars = ST_Object_getIVars(locals[LOC_arr]);
    if (lengthValue <= 0) {
        goto final;
    }
    for (i = 0; i < lengthValue; ++i) {
        ST_Internal_Object **nodeVars;
        locals[LOC_nodeInst] =
            ST_sendMsg(ctx, locals[LOC_cLNode], newSymb, 0, NULL);
        nodeVars = ST_Object_getIVars(locals[LOC_nodeInst]);
        nodeVars[0] = locals[LOC_list];
        locals[LOC_list] = locals[LOC_nodeInst];
    }
    ivars[0] = locals[LOC_list];
    /* TODO: implement clone method for int, or new method that takes a val */
    ivars[1] = ST_sendMsg(ctx, ST_getGlobal(ctx, ST_symb(ctx, "Integer")),
                          newSymb, 0, NULL);
    ST_sendMsg(ctx, ivars[1], ST_symb(ctx, "rawSet:"), 1,
               (ST_Object *)&lengthValue);
    result = locals[LOC_arr];
final:
    ST_popLocals(ctx);
    return result;
}

static ST_Internal_Object **ST_Array_deref(ST_Object ctx, ST_Object arr,
                                           ST_Object idx) {
    ST_Object rgetSymb = ST_symb(ctx, "rawGet");
    ST_Internal_Object **ivars = ST_Object_getIVars(arr);
    const ST_S32 index = (intptr_t)ST_sendMsg(ctx, idx, rgetSymb, 0, NULL);
    ST_S32 length = (intptr_t)ST_sendMsg(ctx, ivars[1], rgetSymb, 0, NULL);
    if (index > -1 && index < length) {
        ST_S32 i = 0;
        ST_Object node = ivars[0];
        while (true) {
            if (i == index) {
                return &ST_Object_getIVars(node)[1];
            }
            ++i;
            node = ST_Object_getIVars(node)[0];
        }
    }
    return NULL;
}

static ST_Object ST_Array_at(ST_Object ctx, ST_Object self, ST_Object argv[]) {
    ST_Internal_Object **element;
    if ((element = ST_Array_deref(ctx, self, argv[0]))) {
        return *element;
    }
    /* TODO: raise exception */
    return ST_getNil(ctx);
}

static ST_Object ST_Array_set(ST_Object ctx, ST_Object self, ST_Object argv[]) {
    ST_Internal_Object **element;
    if ((element = ST_Array_deref(ctx, self, argv[0]))) {
        *element = argv[1];
        return ST_getNil(ctx);
    }
    /* TODO: raise exception */
    return ST_getNil(ctx);
}

static ST_Object ST_Array_len(ST_Object ctx, ST_Object self, ST_Object argv[]) {
    /* FIXME: return a clone!!! */
    return ST_Object_getIVars(self)[1];
}

static void ST_initArray(ST_Context *ctx) {
    ST_Class *cObj = ST_getGlobal(ctx, ST_symb(ctx, "Object"));
    ST_Object arraySymb = ST_symb(ctx, "Array");
    ST_Object listNodeSymb = ST_symb(ctx, "ListNode");
    ST_Class *cArr = ST_Class_subclass(ctx, cObj, arraySymb, 2, 0);
    ST_Class *cNode = ST_Class_subclass(ctx, cObj, listNodeSymb, 2, 0);
    cArr->instanceVariableNames[0] = ST_symb(ctx, "data");
    cArr->instanceVariableNames[1] = ST_symb(ctx, "length");
    cNode->instanceVariableNames[0] = ST_symb(ctx, "next");
    cNode->instanceVariableNames[1] = ST_symb(ctx, "element");
    ST_setGlobal(ctx, listNodeSymb, cNode);
    ST_setMethod(ctx, cArr, ST_symb(ctx, "length"), ST_Array_len, 0);
    ST_setMethod(ctx, cArr, ST_symb(ctx, "new:"), ST_Array_new, 1);
    ST_setMethod(ctx, cArr, ST_symb(ctx, "at:"), ST_Array_at, 1);
    ST_setMethod(ctx, cArr, ST_symb(ctx, "at:put:"), ST_Array_set, 2);
    ST_setGlobal(ctx, arraySymb, cArr);
}

/*//////////////////////////////////////////////////////////////////////////////
// Language types and methods
/////////////////////////////////////////////////////////////////////////////*/

static ST_Object ST_new(ST_Object ctx, ST_Object self, ST_Object argv[]) {
    ST_Class *class = self;
    return ST_Class_makeInstance(ctx, class);
}

static ST_Object ST_subclass(ST_Object ctx, ST_Object self, ST_Object argv[]) {
    return ST_Class_subclass(ctx, self, argv[0], 0, 0);
}

static ST_Object ST_subclassExtended(ST_Object ctx, ST_Object self,
                                     ST_Object argv[]) {
    ST_Class *subc;
    ST_Object rawgetSymb = ST_symb(ctx, "rawGet");
    ST_Object newSymb = ST_symb(ctx, "new");
    ST_Object lenSymb = ST_symb(ctx, "length");
    ST_Object rawsetSymb = ST_symb(ctx, "rawSet:");
    ST_Object atSymb = ST_symb(ctx, "at:");
    enum { LOC_ivarsLen, LOC_cvarsLen, LOC_cInt, LOC_index, LOC_count };
    ST_Object *locals = ST_pushLocals(ctx, LOC_count);
    ST_S32 ivarCount, cvarCount, i;
    locals[LOC_ivarsLen] = ST_sendMsg(ctx, argv[1], lenSymb, 0, NULL);
    locals[LOC_cvarsLen] = ST_sendMsg(ctx, argv[2], lenSymb, 0, NULL);
    locals[LOC_cInt] = ST_getGlobal(ctx, ST_symb(ctx, "Integer"));
    ivarCount =
        (intptr_t)ST_sendMsg(ctx, locals[LOC_ivarsLen], rawgetSymb, 0, NULL);
    cvarCount =
        (intptr_t)ST_sendMsg(ctx, locals[LOC_cvarsLen], rawgetSymb, 0, NULL);
    locals[LOC_index] = ST_sendMsg(ctx, locals[LOC_cInt], newSymb, 0, NULL);
    subc = ST_Class_subclass(ctx, self, argv[0], ivarCount, cvarCount);
    for (i = 0; i < ivarCount; ++i) {
        ST_Object ivarName;
        ST_sendMsg(ctx, locals[LOC_index], rawsetSymb, 1, (ST_Object)&i);
        ivarName = ST_sendMsg(ctx, argv[1], atSymb, 1, &locals[LOC_index]);
        subc->instanceVariableNames[i] = ivarName;
    }
    ST_popLocals(ctx);
    return subc;
}

static ST_Object ST_class(ST_Object ctx, ST_Object self, ST_Object argv[]) {
    return ((ST_Internal_Object *)self)->class;
}

static bool ST_Context_bootstrap(ST_Context *ctx) {
    /* We need to do things manually for a bit, until we've defined the
 symbol class and the new method, because most of the functions in
 the runtime depend on Symbol. */
    ST_Class *cObject = ST_Pool_alloc(ctx, &ctx->classPool);
    ST_Class *cSymbol;
    ST_Internal_Object *symbolSymbol;
    ST_Internal_Object *newSymbol;
    ST_StringMap_Entry *newEntry = ST_Pool_alloc(ctx, &ctx->strmapNodePool);
    ST_BST_Node_init((ST_BST_Node *)newEntry);
    cObject->object.class = cObject;
    cObject->super = NULL;
    cObject->methodTree = NULL;
    cObject->instanceVariableCount = 0;
    cObject->instanceVariableNames = NULL;
    cObject->instanceSize = 0;
    cSymbol = ST_Class_subclass(ctx, cObject, NULL, 0, 0);
    symbolSymbol = ST_Class_makeInstance(ctx, cSymbol);
    newSymbol = ST_Class_makeInstance(ctx, cSymbol);
    ST_Object_setGCMask(symbolSymbol, ST_GC_MASK_PRESERVE);
    ST_Object_setGCMask(newSymbol, ST_GC_MASK_PRESERVE);
    ctx->symbolRegistry = ST_Pool_alloc(ctx, &ctx->strmapNodePool);
    ST_BST_Node_init((ST_BST_Node *)ctx->symbolRegistry);
    ctx->symbolRegistry->key = ST_strdup(ctx, "Symbol");
    ctx->symbolRegistry->value = symbolSymbol;
    ctx->globalScope = ST_Pool_alloc(ctx, &ctx->gvarNodePool);
    ST_BST_Node_init((ST_BST_Node *)ctx->globalScope);
    ctx->globalScope->header.symbol = symbolSymbol;
    ctx->globalScope->value = (ST_Object)cSymbol;
    newEntry->key = ST_strdup(ctx, "new");
    newEntry->value = newSymbol;
    ST_BST_insert((ST_BST_Node **)&ctx->symbolRegistry, &newEntry->nodeHeader,
                  ST_StringMap_comparator);
    ST_setMethod(ctx, cObject, newSymbol, ST_new, 0);
    ST_setGlobal(ctx, ST_symb(ctx, "Object"), cObject);
    return true;
}

static void ST_initNil(ST_Context *ctx) {
    ST_Object cObj = ST_getGlobal(ctx, ST_symb(ctx, "Object"));
    ST_Object undefObjSymb = ST_symb(ctx, "UndefinedObject");
    ST_Object cUndefObj = ST_Class_subclass(ctx, cObj, undefObjSymb, 0, 0);
    ctx->nilValue = ST_sendMsg(ctx, cUndefObj, ST_symb(ctx, "new"), 0, NULL);
    ST_Object_setGCMask(ctx->nilValue, ST_GC_MASK_PRESERVE);
    ST_setGlobal(ctx, undefObjSymb, cUndefObj);
}

static void ST_initBoolean(ST_Context *ctx) {
    ST_Object cObj = ST_getGlobal(ctx, ST_symb(ctx, "Object"));
    ST_Object boolSymb = ST_symb(ctx, "Boolean");
    ST_Object trueSymb = ST_symb(ctx, "True");
    ST_Object falseSymb = ST_symb(ctx, "False");
    ST_Object cBoolean = ST_Class_subclass(ctx, cObj, boolSymb, 0, 0);
    ST_Object cTrue = ST_Class_subclass(ctx, cBoolean, trueSymb, 0, 0);
    ST_Object cFalse = ST_Class_subclass(ctx, cBoolean, falseSymb, 0, 0);
    ST_Object newSymb = ST_symb(ctx, "new");
    ctx->trueValue = ST_sendMsg(ctx, cTrue, newSymb, 0, NULL);
    ctx->falseValue = ST_sendMsg(ctx, cFalse, newSymb, 0, NULL);
    ST_Object_setGCMask(ctx->trueValue, ST_GC_MASK_PRESERVE);
    ST_Object_setGCMask(ctx->falseValue, ST_GC_MASK_PRESERVE);
    ST_setGlobal(ctx, boolSymb, cBoolean);
    ST_setGlobal(ctx, trueSymb, cTrue);
    ST_setGlobal(ctx, falseSymb, cFalse);
}

static void ST_initObject(ST_Context *ctx) {
    ST_Object cObj = ST_getGlobal(ctx, ST_symb(ctx, "Object"));
    ST_setMethod(ctx, cObj, ST_symb(ctx, ST_subcMethodName), ST_subclass, 1);
    ST_setMethod(ctx, cObj, ST_symb(ctx, "class"), ST_class, 0);
    ST_setMethod(ctx, cObj, ST_symb(ctx, ST_subcExtMethodName),
                 ST_subclassExtended, 3);
}

static void ST_initErrorHandling(ST_Context *ctx) {
    ST_Object cObj = ST_getGlobal(ctx, ST_symb(ctx, "Object"));
    ST_Object mnuSymb = ST_symb(ctx, "MessageNotUnderstood");
    ST_Object cMNU = ST_Class_subclass(ctx, cObj, mnuSymb, 0, 0);
    ST_setGlobal(ctx, mnuSymb, cMNU);
}

static ST_Object ST_enableGC(ST_Object ctx, ST_Object self, ST_Object argv[]) {
    ((ST_Context *)self)->gcDisabled = false;
    return ST_getNil(ctx);
}

static ST_Object ST_disableGC(ST_Object ctx, ST_Object self, ST_Object argv[]) {
    ((ST_Context *)self)->gcDisabled = true;
    return ST_getNil(ctx);
}

static void ST_initContext(ST_Context *ctx) {
    ST_Class voidClass;
    ST_Object cCtxSymb;
    ST_Class *cCtx;
    voidClass.instanceVariableCount = 0;
    cCtxSymb = ST_symb(ctx, "Context");
    cCtx = ST_Class_subclass(ctx, &voidClass, cCtxSymb, 0, 0);
    ST_Object_setGCMask(cCtx, ST_GC_MASK_PRESERVE);
    cCtx->super = NULL;
    ctx->object.object.class = cCtx;
    ST_setMethod(ctx, cCtx, ST_symb(ctx, ST_subcMethodName), ST_nopMethod, 1);
    ST_setMethod(ctx, cCtx, ST_symb(ctx, ST_subcExtMethodName), ST_nopMethod,
                 3);
    ST_setMethod(ctx, cCtx, ST_symb(ctx, "disableGC"), ST_disableGC, 0);
    ST_setMethod(ctx, cCtx, ST_symb(ctx, "enableGC"), ST_enableGC, 0);
    ST_setGlobal(ctx, ST_symb(ctx, "SmalltalkContext"), ctx);
}

ST_Object ST_createContext(const ST_Configuration *config) {
    ST_Context *ctx = config->memory.allocFn(sizeof(ST_Context));
    if (!ctx)
        return NULL;
    ctx->config = *config;
    ST_Pool_init(ctx, &ctx->gvarNodePool, sizeof(ST_GlobalVarMap_Entry), 100);
    ST_Pool_init(ctx, &ctx->vmFramePool, sizeof(ST_StackFrame), 50);
    ST_Pool_init(ctx, &ctx->methodNodePool, sizeof(ST_MethodMap_Entry), 512);
    ST_Pool_init(ctx, &ctx->strmapNodePool, sizeof(ST_StringMap_Entry), 512);
    ST_Pool_init(ctx, &ctx->classPool, sizeof(ST_Class), 100);
    ST_Pool_init(ctx, &ctx->integerPool, sizeof(ST_Integer), 128);
    ctx->operandStack.base = ST_alloc(ctx, sizeof(ST_Internal_Object *) *
                                               config->memory.stackCapacity);
    ctx->operandStack.top =
        (struct ST_Internal_Object **)ctx->operandStack.base;
    ctx->heap.begin = ST_alloc(ctx, config->memory.heapCapacity);
    ctx->heap.end = ctx->heap.begin;
    ST_pushStackFrame(ctx, 0, NULL);
    ST_Context_bootstrap(ctx);
    ST_initObject(ctx);
    ST_initContext(ctx);
    ST_initNil(ctx);
    ST_initBoolean(ctx);
    ST_initErrorHandling(ctx);
    ST_initInteger(ctx);
    ST_initArray(ctx);
    return ctx;
}

void ST_destroyContext(ST_Object ctx) {
    ST_Context *ctxImpl = ctx;
    while (ctxImpl->symbolRegistry) {
        ST_StringMap_Entry *removedSymb = (ST_StringMap_Entry *)ST_BST_remove(
            (ST_BST_Node **)&ctxImpl->symbolRegistry, ctxImpl->symbolRegistry,
            ST_StringMap_comparator);
        ST_free(ctx, removedSymb->key);
    }
    ST_free(ctx, ctxImpl->operandStack.base);
    ST_free(ctx, ctxImpl->heap.begin);
    ST_Pool_release(ctx, &ctxImpl->gvarNodePool);
    ST_Pool_release(ctx, &ctxImpl->vmFramePool);
    ST_Pool_release(ctx, &ctxImpl->methodNodePool);
    ST_Pool_release(ctx, &ctxImpl->strmapNodePool);
    ST_Pool_release(ctx, &ctxImpl->classPool);
    ST_Pool_release(ctx, &ctxImpl->integerPool);
    ST_free(ctx, ctx);
}

/*//////////////////////////////////////////////////////////////////////////////
// GC
/////////////////////////////////////////////////////////////////////////////*/

/* FIXME: non-recursive version of markObject */
static void ST_GC_markObject(ST_Context *ctx, ST_Internal_Object *object) {
    ST_Object_setGCMask(object, ST_GC_MASK_MARKED);
    if (!ST_isClass(object)) {
        ST_Internal_Object **ivars = ST_Object_getIVars(object);
        ST_Size i;
        for (i = 0; i < object->class->instanceVariableCount; ++i) {
            ST_GC_markObject(ctx, ivars[i]);
        }
        ST_GC_markObject(ctx, (ST_Object)object->class);
    } else /* We're a class */ {
        ST_Class *class = (ST_Class *)object;
        if (class->super) {
            ST_GC_markObject(ctx, (ST_Object) class->super);
        }
        /* TODO: eventually, mark class variables too */
    }
}

typedef struct ST_GC_Visitor {
    ST_Visitor visitor;
    ST_Context *ctx;
} ST_GC_Visitor;

static void ST_GC_visitGVar(ST_Visitor *visitor, void *gvar) {
    ST_GC_markObject(((ST_GC_Visitor *)visitor)->ctx,
                     ((ST_SymbolMap_Entry *)gvar)->symbol);
}

static void ST_GC_mark(ST_Context *ctx) {
    ST_Size opStackSize = ST_stackSize(ctx);
    ST_Size i;
    ST_GC_Visitor visitor;
    for (i = 0; i < opStackSize; ++i) {
        ST_GC_markObject(ctx, ST_refStack(ctx, i));
    }
    visitor.ctx = ctx;
    visitor.visitor.visit = ST_GC_visitGVar;
    ST_BST_traverse((ST_BST_Node *)ctx->globalScope, (ST_Visitor *)&visitor);
}

static void ST_GC_compact(ST_Context *ctx) {
    /* TODO: implement me! */
    while (true)
        ;
}

static void ST_GC_run(ST_Object ctx) {
    ST_GC_mark(ctx);
    ST_GC_compact(ctx);
    /* TODO: compact */
}

static struct ST_Internal_Object *ST_GC_allocObject(ST_Context *ctx,
                                                    ST_Size objSize) {
    const bool outOfMemory =
        (ST_Size)(ctx->heap.end - ctx->heap.begin) + objSize >=
        ctx->config.memory.heapCapacity;
    ST_Internal_Object *result;
    if (UNEXPECTED(outOfMemory)) {
        ST_GC_run(ctx);
    }
    result = (ST_Internal_Object *)ctx->heap.end;
    ctx->heap.end += objSize;
    return result;
}

/*//////////////////////////////////////////////////////////////////////////////
// Bytecode loading
/////////////////////////////////////////////////////////////////////////////*/

static bool isLittleEndian() {
    const ST_U32 num = 1;
    return ((ST_U8 *)&num)[0] == 1;
}

static void ST_bswap16(ST_U16 *mem) {
    *mem = ((ST_U16)((((ST_U16)(*mem) & (ST_U16)0x00ffU) << 8) |
                     (((ST_U16)(*mem) & (ST_U16)0xff00U) >> 8)));
}

static void ST_bswap32(ST_U32 *mem) {
    *mem = ((ST_U32)((((ST_U32)(*mem) & (ST_U32)0x000000ffU) << 24) |
                     (((ST_U32)(*mem) & (ST_U32)0x0000ff00U) << 8) |
                     (((ST_U32)(*mem) & (ST_U32)0x00ff0000U) >> 8) |
                     (((ST_U32)(*mem) & (ST_U32)0xff000000U) >> 24)));
}

ST_Code ST_VM_load(ST_Object ctx, const ST_U8 *data, ST_Size len) {
    /* Note: symbol table is a list of null-terminated symbol strings, where
       the final symbol in the table is followed by two terminators. */
    ST_Code code;
    ST_Size i, symbCount = 0;
    for (i = 0;; ++i) {
        if (data[i] == '\0') {
            symbCount += 1;
            if (data[i + 1] == '\0') {
                break;
            }
        }
    }
    len -= i + 1;
    code.symbTab = ST_alloc(ctx, sizeof(ST_Object) * symbCount);
    for (i = 0; i < symbCount; ++i) {
        code.symbTab[i] = ST_symb(ctx, (const char *)data);
        data += ST_strlen((const char *)data) + 1;
    }
    ++data;
    code.length = len;
    code.instructions = ST_alloc(ctx, len);
    ST_memcpy(ctx, code.instructions, data, len);
    if (!isLittleEndian()) {
        for (i = 0; i < len; ++i) {
            switch (code.instructions[i]) {
            case ST_VM_OP_PUSHNIL:
            case ST_VM_OP_PUSHTRUE:
            case ST_VM_OP_PUSHFALSE:
            case ST_VM_OP_PUSHSUPER:
            case ST_VM_OP_DUP:
            case ST_VM_OP_POP:
            case ST_VM_OP_SWAP:
            case ST_VM_OP_RETURN:
                break;

            case ST_VM_OP_GETGLOBAL:
            case ST_VM_OP_SETGLOBAL:
            case ST_VM_OP_SENDMSG:
            case ST_VM_OP_GETIVAR:
            case ST_VM_OP_SETIVAR:
            case ST_VM_OP_PUSHSYMBOL:
                ST_bswap16((ST_U16 *)&code.instructions[i + 1]);
                i += sizeof(ST_U16);
                break;

            case ST_VM_OP_SETMETHOD: {
                ST_bswap16((ST_U16 *)&code.instructions[i + 1]);
                i += sizeof(ST_U16) + sizeof(ST_U8);
                ST_bswap32((ST_U32 *)&code.instructions[i + 1]);
                i += sizeof(ST_U32);
                /* TODO */
            } break;
            }
        }
    }
    return code;
}
