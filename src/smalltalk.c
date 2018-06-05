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
static void ST_memset(ST_Context context, void *memory, int val, ST_Size n);
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

static void ST_Pool_grow(ST_Context context, ST_Pool *pool, ST_Size count) {
    const ST_Size poolNodeSize = pool->elemSize + sizeof(ST_Pool_Node);
    const ST_Size headerSize = sizeof(ST_Pool_Slab *);
    const ST_Size allocSize = poolNodeSize * count + headerSize;
    ST_U8 *mem = ST_alloc(context, allocSize);
    ST_Size i;
    ST_memset(context, mem, 0, allocSize);
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
static void ST_Pool_scan(ST_Pool *pool, ST_Visitor *visitor) {
    ST_Pool_Slab *slab = pool->slabs;
    ST_Size slabElemCount = pool->lastAllocCount;
    while (slab) {
        ST_U8 *slabMem = ((ST_U8 *)slab + sizeof(ST_Pool_Slab));
        ST_Size nodeSize = pool->elemSize + sizeof(ST_Pool_Node);
        ST_Size slabSize = slabElemCount * nodeSize;
        ST_Size i;
        for (i = 0; i < slabSize; i += nodeSize) {
            visitor->visit(visitor, slabMem + i + sizeof(ST_Pool_Node));
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

typedef struct ST_SharedInstancePool {
    ST_Pool pool;
    struct ST_SharedInstancePool *next;
} ST_SharedInstancePool;

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
    ST_Pool integerPool;
    ST_SharedInstancePool *sharedPools;
    bool gcPaused;
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

static ST_Size ST_getObjectFootprint(ST_Size ivarCount) {
    return ivarCount * sizeof(ST_Object) + sizeof(ST_Internal_Object);
}

static ST_Pool *ST_getSharedInstancePool(ST_Internal_Context *context,
                                         ST_Size ivarCount) {
    ST_SharedInstancePool *current = context->sharedPools;
    ST_Size targetSize = ST_getObjectFootprint(ivarCount);
    while (current) {
        if (current->pool.elemSize == targetSize) {
            return (ST_Pool *)current;
        }
        current = current->next;
    }
    {
        ST_SharedInstancePool *new =
            ST_alloc(context, sizeof(ST_SharedInstancePool));
        ST_Pool_init(context, (ST_Pool *)new, targetSize, 4);
        new->next = context->sharedPools;
        context->sharedPools = new;
        return (ST_Pool *)new;
    }
}

static void ST_Object_setGCMask(ST_Object obj, enum ST_GC_Mask mask) {
    ((ST_Internal_Object *)obj)->gcMask |= mask;
}

static void ST_Object_unsetGCMask(ST_Object obj, enum ST_GC_Mask mask) {
    ((ST_Internal_Object *)obj)->gcMask &= ~mask;
}

static bool ST_Object_matchGCMask(ST_Object obj, enum ST_GC_Mask mask) {
    return ((ST_Internal_Object *)obj)->gcMask & mask;
}

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
    ST_Object *instanceVariableNames;
    ST_Pool *instancePool;
} ST_Class;

ST_U16 ST_getIVarCount(ST_Context context, ST_Object object) {
    return ((ST_Internal_Object *)object)->class->instanceVariableCount;
}

ST_Object ST_getSuper(ST_Context context, ST_Object object) {
    return ((ST_Internal_Object *)object)->class->super;
}

ST_Internal_Object *ST_Class_makeInstance(ST_Internal_Context *context,
                                          ST_Class *class) {
    ST_Internal_Object *instance = ST_Pool_alloc(context, class->instancePool);
    ST_Internal_Object **ivars = ST_Object_getIVars(instance);
    ST_Size i;
    for (i = 0; i < class->instanceVariableCount; ++i) {
        ivars[i] = ST_getNil(context);
    }
    instance->class = class;
    ST_Object_setGCMask(instance, ST_GC_MASK_ALIVE);
    return instance;
}

static ST_Class *ST_Class_subclass(ST_Internal_Context *context,
                                   ST_Class *class,
                                   ST_Size instanceVariableCount,
                                   ST_Size classVariableCount) {
    ST_Class *sub =
        ST_Pool_alloc(context, &((ST_Internal_Context *)context)->classPool);
    sub->object.class = sub;
    ST_Object_setGCMask(sub, ST_GC_MASK_ALIVE);
    sub->super = class;
    sub->instanceVariableCount =
        ((ST_Class *)class)->instanceVariableCount + instanceVariableCount;
    if (instanceVariableCount) {
        sub->instanceVariableNames = ST_alloc(
            context, instanceVariableCount * sizeof(ST_Internal_Object *));
    } else {
        sub->instanceVariableNames = NULL;
    }
    sub->methodTree = NULL;
    sub->instancePool =
        ST_getSharedInstancePool(context, sub->instanceVariableCount);
    return sub;
}

static bool ST_isClass(ST_Internal_Object *object) {
    return (ST_Class *)object == object->class;
}

static ST_Internal_Method *
ST_Internal_Object_getMethod(ST_Context context, ST_Internal_Object *obj,
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

static void ST_failedMethodLookup(ST_Context ctx, ST_Object receiver,
                                  ST_Object symbol) {
    ST_Object cMNU = ST_getGlobal(ctx, ST_symb(ctx, "MessageNotUnderstood"));
    ST_Object err = ST_sendMsg(ctx, cMNU, ST_symb(ctx, "new"), 0, NULL);
    ST_sendMsg(ctx, receiver, ST_symb(ctx, "doesNotUnderstand:"), 1, &err);
}

ST_Object ST_sendMsg(ST_Context context, ST_Object receiver, ST_Object symbol,
                     ST_U8 argc, ST_Object argv[]) {
    ST_Internal_Method *method =
        ST_Internal_Object_getMethod(context, receiver, symbol);
    if (method) {
        switch (method->type) {
        case ST_METHOD_TYPE_PRIMITIVE:
            if (argc != method->argc) {
                /* FIXME: wrong number of args */
                return ST_getNil(context);
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
    ST_failedMethodLookup(context, receiver, symbol);
    return ST_getNil(context);
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

void ST_setMethod(ST_Context context, ST_Object object, ST_Object symbol,
                  ST_PrimitiveMethod primitiveMethod, ST_U8 argc) {
    ST_Pool *methodPool = &((ST_Internal_Context *)context)->methodNodePool;
    ST_MethodMap_Entry *entry = ST_Pool_alloc(context, methodPool);
    ST_BST_Node_init((ST_BST_Node *)entry);
    entry->header.symbol = symbol;
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
        /* FIXME! */
    }
    return mem;
}

static void ST_free(ST_Context context, void *memory) {
    ((ST_Internal_Context *)context)->config.memory.freeFn(memory);
}

static void ST_memset(ST_Context context, void *memory, int val, ST_Size n) {
    ((ST_Internal_Context *)context)->config.memory.setFn(memory, val, n);
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
        return ST_getNil(context);
    }
    ST_BST_splay(globalScope, &symbol, ST_SymbolMap_comparator);
    return ((ST_GlobalVarMap_Entry *)found)->value;
}

void ST_setGlobal(ST_Context context, ST_Object symbol, ST_Object object) {
    ST_Internal_Context *ctxImpl = context;
    ST_SymbolMap_Entry searchTmpl;
    ST_BST_Node *found;
    searchTmpl.symbol = symbol;
    if (UNEXPECTED(object == ST_getNil(context))) {
        ST_BST_Node *removed =
            ST_BST_remove((ST_BST_Node **)&ctxImpl->globalScope, &searchTmpl,
                          ST_SymbolMap_comparator);
        ST_Pool_free(context, &ctxImpl->gvarNodePool, removed);
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
            ST_Pool_free(context, &ctxImpl->gvarNodePool, entry);
        }
    }
}

ST_Object ST_getNil(ST_Context context) {
    return ((ST_Internal_Context *)context)->nilValue;
}

ST_Object ST_getTrue(ST_Context context) {
    return ((ST_Internal_Context *)context)->trueValue;
}

ST_Object ST_getFalse(ST_Context context) {
    return ((ST_Internal_Context *)context)->falseValue;
}

ST_Object ST_symb(ST_Context ctx, const char *symbolName) {
    ST_Internal_Context *extCtx = ctx;
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

const char *ST_Symbol_toString(ST_Context context, ST_Object symbol) {
    ST_SymbolNameByValueVisitor visitor;
    visitor.visitor.visit = ST_findSymbolNameByValue;
    visitor.key = symbol;
    visitor.result = NULL;
    ST_BST_traverse(
        (ST_BST_Node *)((ST_Internal_Context *)context)->symbolRegistry,
        (ST_Visitor *)&visitor);
    return visitor.result;
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
    ST_VM_OP_SENDMSG,
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
            ST_pushStack(context, ST_getNil(context));
            frame->ip += ST_OPCODE_SIZE;
            break;

        case ST_VM_OP_PUSHTRUE:
            ST_pushStack(context, ST_getTrue(context));
            frame->ip += ST_OPCODE_SIZE;
            break;

        case ST_VM_OP_PUSHFALSE:
            ST_pushStack(context, ST_getFalse(context));
            frame->ip += ST_OPCODE_SIZE;
            break;

        case ST_VM_OP_PUSHSYMBOL:
            ST_pushStack(context, frame->code->symbTab[ST_readLE16(frame, 1)]);
            frame->ip += ST_OPCODE_SIZE + sizeof(ST_U16);
            break;

        case ST_VM_OP_SENDMSG: {
            const ST_Object symbol =
                frame->code->symbTab[ST_readLE16(frame, 1)];
            ST_Object receiver = ST_refStack(context, 0);
            ST_Internal_Method *method =
                ST_Internal_Object_getMethod(context, receiver, symbol);
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
                ST_failedMethodLookup(context, receiver, symbol);
            }
        } break;

        case ST_VM_OP_SETMETHOD: {
            const ST_Object symbol =
                frame->code->symbTab[ST_readLE16(frame, 1)];
            const ST_Object target = ST_refStack(context, 0);
            const ST_U8 argc = frame->code->instructions[frame->ip + 3];
            ST_MethodMap_Entry *entry =
                ST_Pool_alloc(context, &context->methodNodePool);
            ST_BST_Node_init((ST_BST_Node *)entry);
            frame->ip += sizeof(ST_U8) + sizeof(ST_U16);
            entry->header.symbol = symbol;
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
        return ST_getNil(ctx);
    ret = (ST_Integer *)ST_Class_makeInstance(ctx, ST_getClass(ctx, self));
    ret->value = ((ST_Integer *)self)->value + ((ST_Integer *)argv[0])->value;
    return ret;
}

static ST_Object ST_Integer_sub(ST_Context ctx, ST_Object self,
                                ST_Object argv[]) {
    ST_Integer *ret;
    if (!ST_Integer_typecheck(self, argv[0]))
        return ST_getNil(ctx);
    ret = (ST_Integer *)ST_Class_makeInstance(ctx, ST_getClass(ctx, self));
    ret->value = ((ST_Integer *)self)->value - ((ST_Integer *)argv[0])->value;
    return ret;
}

static ST_Object ST_Integer_mul(ST_Context ctx, ST_Object self,
                                ST_Object argv[]) {
    ST_Integer *ret;
    if (!ST_Integer_typecheck(self, argv[0]))
        return ST_getNil(ctx);
    ret = (ST_Integer *)ST_Class_makeInstance(ctx, ST_getClass(ctx, self));
    ret->value = ((ST_Integer *)self)->value * ((ST_Integer *)argv[0])->value;
    return ret;
}

static ST_Object ST_Integer_div(ST_Context ctx, ST_Object self,
                                ST_Object argv[]) {
    ST_Integer *ret;
    if (!ST_Integer_typecheck(self, argv[0]))
        return ST_getNil(ctx);
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
    return ST_getNil(context);
}

static void ST_initInteger(ST_Internal_Context *ctx) {
    ST_Class *cObj = ST_getGlobal(ctx, ST_symb(ctx, "Object"));
    ST_Class *cInt = ST_Pool_alloc(ctx, &ctx->classPool);
    cInt->instanceVariableCount = 0;
    cInt->instanceVariableNames = NULL;
    cInt->instancePool = &ctx->integerPool;
    cInt->methodTree = NULL;
    cInt->super = cObj;
    cInt->object.class = cInt;
    ST_Object_setGCMask(cInt, ST_GC_MASK_ALIVE);
    ST_setMethod(ctx, cInt, ST_symb(ctx, "+"), ST_Integer_add, 1);
    ST_setMethod(ctx, cInt, ST_symb(ctx, "-"), ST_Integer_sub, 1);
    ST_setMethod(ctx, cInt, ST_symb(ctx, "*"), ST_Integer_mul, 1);
    ST_setMethod(ctx, cInt, ST_symb(ctx, "/"), ST_Integer_div, 1);
    ST_setMethod(ctx, cInt, ST_symb(ctx, "rawSet:"), ST_Integer_rawSet, 1);
    ST_setMethod(ctx, cInt, ST_symb(ctx, "rawGet"), ST_Integer_rawGet, 0);
    ST_setGlobal(ctx, ST_symb(ctx, "Integer"), cInt);
}

/*//////////////////////////////////////////////////////////////////////////////
// Array
/////////////////////////////////////////////////////////////////////////////*/

static ST_Object ST_Array_new(ST_Context ctx, ST_Object self,
                              ST_Object argv[]) {
    ST_Object rgetSymb = ST_symb(ctx, "rawGet");
    ST_Object lengthParam = argv[0];
    ST_Internal_Object *arr = ST_Class_makeInstance(ctx, self);
    ST_Internal_Object **ivars = ST_Object_getIVars(arr);
    ST_Integer_Rep lengthValue =
        (intptr_t)ST_sendMsg(ctx, lengthParam, rgetSymb, 0, NULL);
    ST_Object newSymb = ST_symb(ctx, "new");
    ST_Object cNode = ST_getGlobal(ctx, ST_symb(ctx, "ListNode"));
    ST_Object list = ST_getNil(ctx);
    ST_Integer_Rep i;
    if (lengthValue <= 0) {
        /* TODO: Raise error for zero length array? */
        return ST_getNil(ctx);
    }
    for (i = 0; i < lengthValue; ++i) {
        ST_Object node = ST_sendMsg(ctx, cNode, newSymb, 0, NULL);
        ST_Internal_Object **nodeVars = ST_Object_getIVars(node);
        nodeVars[0] = list;
        list = node;
    }
    ivars[0] = list;
    /* TODO: implement clone method for int, or new method that takes a val */
    ivars[1] = ST_sendMsg(ctx, ST_getGlobal(ctx, ST_symb(ctx, "Integer")),
                          newSymb, 0, NULL);
    ST_sendMsg(ctx, ivars[1], ST_symb(ctx, "rawSet:"), 1,
               (ST_Object *)&lengthValue);
    return arr;
}

static ST_Object ST_Array_at(ST_Context context, ST_Object self,
                             ST_Object argv[]) {
    ST_Object rgetSymb = ST_symb(context, "rawGet");
    ST_Internal_Object **ivars = ST_Object_getIVars(self);
    ST_Integer_Rep index =
        (intptr_t)ST_sendMsg(context, argv[0], rgetSymb, 0, NULL);
    ST_Integer_Rep length =
        (intptr_t)ST_sendMsg(context, ivars[1], rgetSymb, 0, NULL);
    if (index > -1 && index < length) {
        ST_Integer_Rep i = 0;
        ST_Object node = ivars[0];
        while (true) {
            if (i == index) {
                return ST_Object_getIVars(node)[1];
            }
            ++i;
            node = ST_Object_getIVars(node)[0];
        }
    }
    return ST_getNil(context);
}

static ST_Object ST_Array_set(ST_Context context, ST_Object self,
                              ST_Object argv[]) {
    ST_Object rgetSymb = ST_symb(context, "rawGet");
    ST_Internal_Object **ivars = ST_Object_getIVars(self);
    ST_Integer_Rep index =
        (intptr_t)ST_sendMsg(context, argv[0], rgetSymb, 0, NULL);
    ST_Integer_Rep length =
        (intptr_t)ST_sendMsg(context, ivars[1], rgetSymb, 0, NULL);
    if (index > -1 && index < length) {
        ST_Integer_Rep i = 0;
        ST_Object node = ivars[0];
        while (true) {
            if (i == index) {
                ST_Object_getIVars(node)[1] = argv[1];
                break;
            }
            ++i;
            node = ST_Object_getIVars(node)[0];
        }
    }
    return ST_getNil(context);
}

static ST_Object ST_Array_len(ST_Context context, ST_Object self,
                              ST_Object argv[]) {
    /* FIXME: return a clone!!! */
    return ST_Object_getIVars(self)[1];
}

static void ST_initArray(ST_Internal_Context *ctx) {
    ST_Class *cObj = ST_getGlobal(ctx, ST_symb(ctx, "Object"));
    ST_Class *cArr = ST_Class_subclass(ctx, cObj, 2, 0);
    ST_Class *cNode = ST_Class_subclass(ctx, cObj, 2, 0);
    cArr->instanceVariableNames[0] = ST_symb(ctx, "data");
    cArr->instanceVariableNames[1] = ST_symb(ctx, "length");
    cNode->instanceVariableNames[0] = ST_symb(ctx, "next");
    cNode->instanceVariableNames[1] = ST_symb(ctx, "element");
    ST_setGlobal(ctx, ST_symb(ctx, "ListNode"), cNode);
    ST_setMethod(ctx, cArr, ST_symb(ctx, "length"), ST_Array_len, 0);
    ST_setMethod(ctx, cArr, ST_symb(ctx, "new:"), ST_Array_new, 1);
    ST_setMethod(ctx, cArr, ST_symb(ctx, "at:"), ST_Array_at, 1);
    ST_setMethod(ctx, cArr, ST_symb(ctx, "at:put:"), ST_Array_set, 2);
    ST_setGlobal(ctx, ST_symb(ctx, "Array"), cArr);
}

/*//////////////////////////////////////////////////////////////////////////////
// GC
/////////////////////////////////////////////////////////////////////////////*/

/* FIXME: non-recursive version of markObject */
static void ST_GC_markObject(ST_Internal_Context *context,
                             ST_Internal_Object *object) {
    ST_Object_setGCMask(object, ST_GC_MASK_MARKED);
    if (!ST_isClass(object)) {
        ST_Internal_Object **ivars = ST_Object_getIVars(object);
        ST_Size i;
        for (i = 0; i < object->class->instanceVariableCount; ++i) {
            ST_GC_markObject(context, ivars[i]);
        }
        ST_GC_markObject(context, (ST_Object)object->class);
    } else /* We're a class */ {
        ST_Class *class = (ST_Class *)object;
        if (class->super) {
            ST_GC_markObject(context, (ST_Object) class->super);
        }
        /* TODO: eventually, mark class variables too */
    }
}

typedef struct ST_GC_Visitor {
    ST_Visitor visitor;
    ST_Internal_Context *context;
} ST_GC_Visitor;

static void ST_GC_visitGVar(ST_Visitor *visitor, void *gvar) {
    ST_GC_markObject(((ST_GC_Visitor *)visitor)->context,
                     ((ST_SymbolMap_Entry *)gvar)->symbol);
}

static void ST_GC_mark(ST_Internal_Context *context) {
    ST_Size opStackSize = ST_stackSize(context);
    ST_Size i;
    ST_GC_Visitor visitor;
    for (i = 0; i < opStackSize; ++i) {
        ST_GC_markObject(context, ST_refStack(context, i));
    }
    visitor.context = context;
    visitor.visitor.visit = ST_GC_visitGVar;
    ST_BST_traverse((ST_BST_Node *)context->globalScope,
                    (ST_Visitor *)&visitor);
}

static void ST_GC_sweepVisitInstance(ST_Visitor *visitor, void *instanceMem) {
    ST_Internal_Object *obj = instanceMem;
    ST_Internal_Context *context = ((ST_GC_Visitor *)visitor)->context;
    if (ST_Object_matchGCMask(obj, ST_GC_MASK_ALIVE)) {
        if (ST_Object_matchGCMask(obj,
                                  ST_GC_MASK_MARKED | ST_GC_MASK_PRESERVE)) {
            ST_Object_unsetGCMask(obj, ST_GC_MASK_MARKED);
        } else {
            obj->gcMask = 0;
            ST_Pool_free(context, obj->class->instancePool, obj);
        }
    }
}

static void ST_GC_sweepVisitClass(ST_Visitor *visitor, void *classMem) {
    ST_Class *class = classMem;
    ST_Internal_Context *context = ((ST_GC_Visitor *)visitor)->context;
    if (ST_Object_matchGCMask(class, ST_GC_MASK_ALIVE)) {
        if (ST_Object_matchGCMask(class,
                                  ST_GC_MASK_MARKED | ST_GC_MASK_PRESERVE)) {
            ST_Object_unsetGCMask(class, ST_GC_MASK_MARKED);
        } else {
            while (class->methodTree) {
                ST_BST_Node *removed =
                    ST_BST_remove((ST_BST_Node **)&class->methodTree,
                                  class->methodTree, ST_SymbolMap_comparator);
                ST_Pool_free(context, &context->methodNodePool, removed);
            }
            class->object.gcMask = 0;
            /* FIXME: free class methods! */
            ST_Pool_free(context, &context->classPool, class);
        }
    }
}

static void ST_GC_sweep(ST_Internal_Context *context) {
    ST_GC_Visitor visitor;
    visitor.visitor.visit = ST_GC_sweepVisitClass;
    visitor.context = context;
    ST_Pool_scan(&context->classPool, (ST_Visitor *)&visitor);
    visitor.visitor.visit = ST_GC_sweepVisitInstance;
    ST_Pool_scan(&context->integerPool, (ST_Visitor *)&visitor);
    {
        ST_SharedInstancePool *sharedPool = context->sharedPools;
        while (sharedPool) {
            ST_Pool_scan(&sharedPool->pool, (ST_Visitor *)&visitor);
            sharedPool = sharedPool->next;
        }
    }
}

void ST_GC_run(ST_Context context) {
    if (!((ST_Internal_Context *)context)->gcPaused) {
        ST_GC_mark(context);
        ST_GC_sweep(context);
    }
}

void ST_GC_preserve(ST_Context context, ST_Object object) {
    ST_Object_setGCMask(object, ST_GC_MASK_PRESERVE);
}

void ST_GC_release(ST_Context context, ST_Object object) {
    ST_Object_unsetGCMask(object, ST_GC_MASK_PRESERVE);
}

void ST_GC_pause(ST_Context context) {
    ((ST_Internal_Context *)context)->gcPaused = true;
}

void ST_GC_resume(ST_Context context) {
    ((ST_Internal_Context *)context)->gcPaused = false;
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
    return ST_Class_subclass(context, self, 0, 0);
}

static ST_Object ST_subclassExtended(ST_Context context, ST_Object self,
                                     ST_Object argv[]) {
    ST_Class *subc;
    ST_GC_pause(context);
    {
        ST_Object rawgetSymb = ST_symb(context, "rawGet");
        ST_Object cInteger = ST_getGlobal(context, ST_symb(context, "Integer"));
        ST_Object newSymb = ST_symb(context, "new");
        ST_Object lenSymb = ST_symb(context, "length");
        ST_Object rawsetSymb = ST_symb(context, "rawSet:");
        ST_Object atSymb = ST_symb(context, "at:");
        ST_Object ivarNamesLength =
            ST_sendMsg(context, argv[1], lenSymb, 0, NULL);
        ST_Object cvarNamesLength =
            ST_sendMsg(context, argv[2], lenSymb, 0, NULL);
        ST_Integer_Rep ivarCount =
            (intptr_t)ST_sendMsg(context, ivarNamesLength, rawgetSymb, 0, NULL);
        ST_Integer_Rep cvarCount =
            (intptr_t)ST_sendMsg(context, cvarNamesLength, rawgetSymb, 0, NULL);
        ST_Object index = ST_sendMsg(context, cInteger, newSymb, 0, NULL);
        ST_Integer_Rep i;
        subc = ST_Class_subclass(context, self, ivarCount, cvarCount);
        for (i = 0; i < ivarCount; ++i) {
            ST_Object ivarName;
            ST_sendMsg(context, index, rawsetSymb, 1, (ST_Object)&i);
            ivarName = ST_sendMsg(context, argv[1], atSymb, 1, &index);
            subc->instanceVariableNames[i] = ivarName;
        }
    }
    ST_GC_resume(context);
    return subc;
}

static ST_Object ST_class(ST_Context context, ST_Object self,
                          ST_Object argv[]) {
    return ((ST_Internal_Object *)self)->class;
}

static ST_Object ST_doesNotUnderstand(ST_Context context, ST_Object self,
                                      ST_Object argv[]) {
    while (1)
        ;
    return ST_getNil(context);
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
    cObject->super = NULL;
    cObject->methodTree = NULL;
    cObject->instanceVariableCount = 0;
    cObject->instanceVariableNames = NULL;
    cObject->instancePool = ST_getSharedInstancePool(context, 0);
    cSymbol = ST_Class_subclass(context, cObject, 0, 0);
    symbolSymbol = ST_Class_makeInstance(context, cSymbol);
    newSymbol = ST_Class_makeInstance(context, cSymbol);
    ST_Object_setGCMask(symbolSymbol, ST_GC_MASK_PRESERVE);
    ST_Object_setGCMask(newSymbol, ST_GC_MASK_PRESERVE);
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
    ST_setGlobal(context, ST_symb(context, "Object"), cObject);
    return true;
}

static ST_Object ST_ifTrueImplForTrue(ST_Context context, ST_Object self,
                                      ST_Object argv[]) {
    ST_Object valueSymbol = ST_symb(context, "value");
    return ST_sendMsg(context, argv[0], valueSymbol, 0, NULL);
}

static ST_Object ST_ifFalseImplForFalse(ST_Context context, ST_Object self,
                                        ST_Object argv[]) {
    ST_Object valueSymbol = ST_symb(context, "value");
    return ST_sendMsg(context, argv[0], valueSymbol, 0, NULL);
}

static ST_Object ST_nopMethod(ST_Context context, ST_Object self,
                              ST_Object argv[]) {
    return ST_getNil(context);
}

static void ST_initNil(ST_Internal_Context *ctx) {
    ST_Object cObj = ST_getGlobal(ctx, ST_symb(ctx, "Object"));
    ST_Object cUndefObj = ST_Class_subclass(ctx, cObj, 0, 0);
    ctx->nilValue = ST_sendMsg(ctx, cUndefObj, ST_symb(ctx, "new"), 0, NULL);
    ST_Object_setGCMask(ctx->nilValue, ST_GC_MASK_PRESERVE);
    ST_setGlobal(ctx, ST_symb(ctx, "UndefinedObject"), cUndefObj);
}

static void ST_initBoolean(ST_Internal_Context *ctx) {
    ST_Object cObj = ST_getGlobal(ctx, ST_symb(ctx, "Object"));
    ST_Object cBoolean = ST_Class_subclass(ctx, cObj, 0, 0);
    ST_Object cTrue = ST_Class_subclass(ctx, cBoolean, 0, 0);
    ST_Object cFalse = ST_Class_subclass(ctx, cBoolean, 0, 0);
    ST_Object ifTrueSymb = ST_symb(ctx, "ifTrue:");
    ST_Object ifFalseSymb = ST_symb(ctx, "ifFalse:");
    ST_Object newSymb = ST_symb(ctx, "new");
    ST_setMethod(ctx, cTrue, ifTrueSymb, ST_ifTrueImplForTrue, 1);
    ST_setMethod(ctx, cTrue, ifFalseSymb, ST_nopMethod, 1);
    ST_setMethod(ctx, cFalse, ifTrueSymb, ST_nopMethod, 1);
    ST_setMethod(ctx, cFalse, ifFalseSymb, ST_ifFalseImplForFalse, 1);
    ctx->trueValue = ST_sendMsg(ctx, cTrue, newSymb, 0, NULL);
    ctx->falseValue = ST_sendMsg(ctx, cFalse, newSymb, 0, NULL);
    ST_Object_setGCMask(ctx->trueValue, ST_GC_MASK_PRESERVE);
    ST_Object_setGCMask(ctx->falseValue, ST_GC_MASK_PRESERVE);
    ST_setGlobal(ctx, ST_symb(ctx, "Boolean"), cBoolean);
    ST_setGlobal(ctx, ST_symb(ctx, "True"), cTrue);
    ST_setGlobal(ctx, ST_symb(ctx, "False"), cFalse);
}

static void ST_initObject(ST_Internal_Context *context) {
    ST_Object cObj = ST_getGlobal(context, ST_symb(context, "Object"));
    ST_setMethod(context, cObj, ST_symb(context, "subclass"), ST_subclass, 0);
    ST_setMethod(context, cObj, ST_symb(context, "class"), ST_class, 0);
    ST_setMethod(
        context, cObj,
        ST_symb(context, "subclass:instanceVariableNames:classVariableNames:"),
        ST_subclassExtended, 3);
}

static void ST_initErrorHandling(ST_Internal_Context *ctx) {
    ST_Object cObj = ST_getGlobal(ctx, ST_symb(ctx, "Object"));
    ST_Object cMNU = ST_Class_subclass(ctx, cObj, 0, 0);
    ST_setGlobal(ctx, ST_symb(ctx, "MessageNotUnderstood"), cMNU);
    ST_setMethod(ctx, cObj, ST_symb(ctx, "doesNotUnderstand:"),
                 ST_doesNotUnderstand, 1);
}

ST_Context ST_createContext(const ST_Context_Configuration *config) {
    ST_Internal_Context *ctx =
        config->memory.allocFn(sizeof(ST_Internal_Context));
    if (!ctx)
        return NULL;
    ctx->config = *config;
    ctx->sharedPools = NULL;
    ST_GC_pause(ctx);
    ST_Pool_init(ctx, &ctx->gvarNodePool, sizeof(ST_GlobalVarMap_Entry), 100);
    ST_Pool_init(ctx, &ctx->vmFramePool, sizeof(ST_VM_Frame), 50);
    ST_Pool_init(ctx, &ctx->methodNodePool, sizeof(ST_MethodMap_Entry), 512);
    ST_Pool_init(ctx, &ctx->strmapNodePool, sizeof(ST_StringMap_Entry), 512);
    ST_Pool_init(ctx, &ctx->classPool, sizeof(ST_Class), 100);
    ST_Pool_init(ctx, &ctx->integerPool, sizeof(ST_Integer), 128);
    ctx->operandStack.base = ST_alloc(ctx, sizeof(ST_Internal_Object *) *
                                               config->memory.stackCapacity);
    ctx->operandStack.top =
        (struct ST_Internal_Object **)ctx->operandStack.base;
    ST_Internal_Context_bootstrap(ctx);
    ST_initObject(ctx);
    ST_initNil(ctx);
    ST_initBoolean(ctx);
    ST_initErrorHandling(ctx);
    ST_initInteger(ctx);
    ST_initArray(ctx);
    ST_GC_resume(ctx);
    return ctx;
}

void ST_destroyContext(ST_Context context) {
    ST_Internal_Context *ctxImpl = context;
    while (ctxImpl->globalScope) {
        ST_BST_Node *removedGVar =
            ST_BST_remove((ST_BST_Node **)&ctxImpl->globalScope,
                          ctxImpl->globalScope, ST_SymbolMap_comparator);
        ST_Pool_free(context, &ctxImpl->gvarNodePool, removedGVar);
    }
    while (ctxImpl->symbolRegistry) {
        ST_StringMap_Entry *removedSymb = (ST_StringMap_Entry *)ST_BST_remove(
            (ST_BST_Node **)&ctxImpl->symbolRegistry, ctxImpl->symbolRegistry,
            ST_StringMap_comparator);
        ST_Object_unsetGCMask(removedSymb->value, ST_GC_MASK_PRESERVE);
        ST_free(context, removedSymb->key);
        ST_Pool_free(context, &ctxImpl->strmapNodePool, removedSymb);
    }
    ST_Object_unsetGCMask(ctxImpl->nilValue, ST_GC_MASK_PRESERVE);
    ST_Object_unsetGCMask(ctxImpl->trueValue, ST_GC_MASK_PRESERVE);
    ST_Object_unsetGCMask(ctxImpl->falseValue, ST_GC_MASK_PRESERVE);
    ctxImpl->operandStack.top =
        (struct ST_Internal_Object **)ctxImpl->operandStack.base;
    ST_GC_run(context);
    ST_free(context, ctxImpl->operandStack.base);
    ST_Pool_release(context, &ctxImpl->gvarNodePool);
    ST_Pool_release(context, &ctxImpl->vmFramePool);
    ST_Pool_release(context, &ctxImpl->methodNodePool);
    ST_Pool_release(context, &ctxImpl->strmapNodePool);
    ST_Pool_release(context, &ctxImpl->classPool);
    ST_Pool_release(context, &ctxImpl->integerPool);
    while (ctxImpl->sharedPools) {
        ST_Pool_release(context, &ctxImpl->sharedPools->pool);
        ctxImpl->sharedPools = ctxImpl->sharedPools->next;
    }
    ST_free(context, context);
}
