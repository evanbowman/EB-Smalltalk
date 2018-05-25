#include "smalltalk.h"

struct ST_Internal_Context;

static void ST_pushStack(struct ST_Internal_Context *context, ST_Object val);
static void ST_popStack(struct ST_Internal_Context *context);
static ST_Object ST_refStack(struct ST_Internal_Context *context,
                             ST_Size offset);

static ST_Size ST_stackSize(struct ST_Internal_Context *context);

static void *ST_alloc(ST_Context context, ST_Size size);
static void ST_free(ST_Context context, void *memory);
static void ST_copy(ST_Context context, void *dst, const void *src,
                    ST_Size bytes);

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
// Pool
/////////////////////////////////////////////////////////////////////////////*/

typedef struct ST_Pool_Node {
    struct ST_Pool_Node *next;
    /* Technically the allocated memory lives in-place here in the node, but
     because this is ansi c, we don't have flexible structs yet, so you have
     to use your imagination */
} ST_Pool_Node;

typedef struct ST_Pool {
    ST_Size elemSize;
    ST_Pool_Node *freelist;
    ST_Size lastAllocCount;
    /* TODO: store slabs so we can free them */
} ST_Pool;

void ST_Pool_grow(ST_Context context, ST_Pool *pool, ST_Size count) {
    const ST_Size poolNodeSize = pool->elemSize + sizeof(ST_Pool_Node);
    const ST_Size allocSize = poolNodeSize * count;
    ST_Byte *mem = ST_alloc(context, allocSize);
    ST_Size i;
    pool->lastAllocCount = count;
    for (i = 0; i < allocSize; i += poolNodeSize) {
        ST_Pool_Node *node = (void *)(mem + i);
        node->next = pool->freelist;
        pool->freelist = node;
    }
}

void ST_Pool_init(ST_Context context, ST_Pool *pool, ST_Size elemSize,
                  ST_Size count) {
    pool->freelist = NULL;
    pool->elemSize = elemSize;
    ST_Pool_grow(context, pool, count);
}

void *ST_Pool_alloc(ST_Context context, ST_Pool *pool) {
    ST_Pool_Node *ret;
    if (!pool->freelist) {
        ST_Pool_grow(context, pool, pool->lastAllocCount * 2);
    }
    ret = pool->freelist;
    pool->freelist = pool->freelist->next;
    /* Note: advancing the node pointer by the size of the node effectively
     strips the header, thus setting the pointer to the contained element.
     see comment in ST_Pool_Node. */
    return (ST_Byte *)ret + sizeof(ST_Pool_Node);
}

void ST_Pool_free(ST_Context context, ST_Pool *pool, void *mem) {
    ST_Pool_Node *node = (void *)((char *)mem - sizeof(ST_Pool_Node));
    node->next = pool->freelist;
    pool->freelist = node;
}

/*//////////////////////////////////////////////////////////////////////////////
// Method
/////////////////////////////////////////////////////////////////////////////*/

typedef ST_Method ST_ForeignMethod;
typedef struct ST_CompiledMethod {
    const ST_Code *source;
    ST_Size offset;
} ST_CompiledMethod;

typedef struct ST_Internal_Method {
    enum { ST_METHOD_TYPE_FOREIGN, ST_METHOD_TYPE_COMPILED } type;
    union {
        ST_ForeignMethod foreignMethod;
        ST_CompiledMethod compiledMethod;
    } payload;
    ST_Byte argc;
} ST_Internal_Method;

/*//////////////////////////////////////////////////////////////////////////////
// Object
/////////////////////////////////////////////////////////////////////////////*/

typedef struct ST_MethodMap_Entry {
    ST_SymbolMap_Entry header;
    ST_Internal_Method method;
} ST_MethodMap_Entry;

typedef struct ST_Internal_Object {
    struct ST_Class *class;
    /* Note:
       Unless an object is a class, there's an instance variable array inlined
       at the end of the struct.
       struct ST_Internal_Object *InstanceVariables[] */
} ST_Internal_Object;

ST_Internal_Object **ST_Object_getIVars(ST_Internal_Object *object) {
    return (void *)((ST_Byte *)object + sizeof(ST_Internal_Object));
}

typedef struct ST_Class {
    ST_Internal_Object object;
    ST_MethodMap_Entry *methodTree;
    struct ST_Class *super;
    ST_Size instanceVariableCount;
    ST_Pool instancePool;
} ST_Class;

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
            /* Note: the current dummy implementation of metaclasses works by having a
         class
         hold a circular reference to itself, so we need to test
         self/super-equality
         before walking up the meta-class hierarchy. */
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
    ST_Object_sendMessage(context, receiver,
                          ST_requestSymbol(context, "doesNotUnderstand"), 1,
                          &err);
}

ST_Object ST_Object_sendMessage(ST_Context context, ST_Object receiver,
                                ST_Object selector, ST_Byte argc,
                                ST_Object argv[]) {
    ST_Internal_Method *method =
        ST_Internal_Object_getMethod(context, receiver, selector);
    if (method) {
        switch (method->type) {
        case ST_METHOD_TYPE_FOREIGN:
            if (argc != method->argc) {
                /* FIXME: wrong number of args */
                return ST_getNilValue(context);
            }
            return method->payload.foreignMethod(context, receiver, argv);

        case ST_METHOD_TYPE_COMPILED: {
            ST_Byte i;
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

/* FIXME.. rename to ST_Class_setMethod? Or just ST_setMethod? */
void ST_Object_setMethod(ST_Context context, ST_Object object,
                         ST_Object selector, ST_ForeignMethod foreignMethod,
                         ST_Byte argc) {
    ST_MethodMap_Entry *entry = ST_alloc(context, sizeof(ST_MethodMap_Entry));
    entry->header.symbol = selector;
    entry->method.type = ST_METHOD_TYPE_FOREIGN;
    entry->method.payload.foreignMethod = foreignMethod;
    entry->method.argc = argc;
    if (!ST_Class_insertMethodEntry(
            context, ((ST_Internal_Object *)object)->class, entry)) {
        ST_free(context, entry);
    }
}

/*//////////////////////////////////////////////////////////////////////////////
// Vector
/////////////////////////////////////////////////////////////////////////////*/

typedef struct ST_Vector {
    char *begin;
    char *end;
    ST_Size capacity;
    ST_Size elemSize;
} ST_Vector;

static bool ST_Vector_init(ST_Context context, ST_Vector *vec, ST_Size elemSize,
                           ST_Size capacity) {
    vec->begin = ST_alloc(context, elemSize * capacity);
    if (UNEXPECTED(!vec->begin))
        return false;
    vec->capacity = capacity;
    vec->end = vec->begin;
    vec->elemSize = elemSize;
    return true;
}

static ST_Size ST_Vector_len(ST_Vector *vec) {
    return (vec->end - vec->begin) / vec->elemSize;
}

static bool ST_Vector_push(ST_Context context, ST_Vector *vec,
                           const void *element) {
    const ST_Size len = ST_Vector_len(vec);
    if (UNEXPECTED(len == vec->capacity)) {
        void *newBuffer;
        vec->capacity = vec->capacity * 2;
        newBuffer = ST_alloc(context, vec->capacity * vec->elemSize);
        ST_copy(context, newBuffer, vec->begin, len * vec->elemSize);
        ST_free(context, vec->begin);
        vec->begin = newBuffer;
        vec->end = vec->begin + len;
    }
    ST_copy(context, vec->end, element, vec->elemSize);
    vec->end += vec->elemSize;
    return true;
}

static void ST_Vector_pop(ST_Vector *vec) { vec->end -= vec->elemSize; }

static void *ST_Vector_get(ST_Vector *vec, ST_Size index) {
    return vec->begin + index * vec->elemSize;
}

__attribute__((unused)) static void ST_Vector_free(ST_Context context,
                                                   ST_Vector *vec) {
    ST_free(context, vec->begin);
}

/*//////////////////////////////////////////////////////////////////////////////
// Context
/////////////////////////////////////////////////////////////////////////////*/

typedef struct ST_Internal_Context {
    ST_Context_Configuration config;
    struct ST_StringMap_Entry *symbolRegistry;
    struct ST_GlobalVarMap_Entry *globalScope;
    ST_Internal_Object *nilValue;
    ST_Internal_Object *trueValue;
    ST_Internal_Object *falseValue;
    ST_Vector operandStack;
    ST_Pool gvarNodePool;
    ST_Pool vmFramePool;
} ST_Internal_Context;

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

static void ST_copy(ST_Context context, void *dst, const void *src,
                    ST_Size bytes) {
    ((ST_Internal_Context *)context)->config.memory.copyFn(dst, src, bytes);
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
    ST_Vector_push(context, &context->operandStack, &val);
}

static void ST_popStack(struct ST_Internal_Context *context) {
    ST_Vector_pop(&context->operandStack);
}

static ST_Object ST_refStack(struct ST_Internal_Context *context,
                             ST_Size offset) {
    ST_Vector *stack = &context->operandStack;
    void *result = ST_Vector_get(stack, ST_Vector_len(stack) - 1);
    if (UNEXPECTED(!result)) {
        return ST_getNilValue(context);
    }
    return *(ST_Internal_Object **)result;
}

static ST_Size ST_stackSize(struct ST_Internal_Context *context) {
    return ST_Vector_len(&context->operandStack);
}

ST_Object ST_getGlobal(ST_Context context, ST_Object symbol) {
    ST_BST_Node *found;
    ST_BST_Node **globalScope =
        (void *)&((ST_Internal_Context *)context)->globalScope;
    ST_SymbolMap_Entry searchTmpl;
    searchTmpl.symbol = symbol;
    found = ST_BST_find(globalScope, &searchTmpl, ST_SymbolMap_comparator);
    if (UNEXPECTED(!found)) {
        /* printf("warning: global variable \"%s\" not found\n", */
        /*        ST_Symbol_toString(context, symbol)); */
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
        ST_free(context, entry);
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
    newEntry = ST_alloc(context, sizeof(ST_StringMap_Entry));
    ST_BST_Node_init((ST_BST_Node *)newEntry);
    newEntry->key = ST_strdup(context, symbolName);
    newEntry->value = ST_NEW(context, "Symbol");
    if (!ST_BST_insert((ST_BST_Node **)&extCtx->symbolRegistry,
                       &newEntry->nodeHeader, ST_StringMap_comparator)) {
        ST_free(context, newEntry->key);
        ST_free(context, newEntry);
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

static void ST_VM_invokeForeignMethod_NArg(ST_Internal_Context *context,
                                           ST_Object receiver,
                                           ST_Internal_Method *method) {
    ST_Object *argv = ST_alloc(context, sizeof(ST_Object) * method->argc);
    ST_Byte i;
    for (i = 0; i < method->argc; ++i) {
        argv[i] = ST_refStack(context, 0);
        ST_popStack(context);
    }
    ST_pushStack(context,
                 method->payload.foreignMethod(context, receiver, argv));
    ST_free(context, argv);
}

typedef uint16_t ST_LE16;
typedef uint32_t ST_LE32;

typedef struct ST_VM_Frame {
    ST_Size ip;
    ST_Size bp;
    const ST_Code *code;
    struct ST_VM_Frame *parent;
} ST_VM_Frame;

static ST_LE16 ST_readLE16(const ST_VM_Frame *f, ST_Size offset) {
    const ST_Byte *base = f->code->instructions + f->ip + offset;
    return ((ST_LE16)*base) | ((ST_LE16) * (base + 1) << 8);
}

static ST_LE16 ST_readLE32(const ST_VM_Frame *f, ST_Size offset) {
    const ST_Byte *base = f->code->instructions + f->ip + offset;
    return ((ST_LE16)*base) | ((ST_LE16) * (base + 1) << 8) |
           ((ST_LE16) * (base + 2) << 16) | ((ST_LE16) * (base + 3) << 24);
}

static void ST_Internal_VM_execute(ST_Internal_Context *context,
                                   ST_VM_Frame *frame) {
    while (frame->ip < frame->code->length) {
        switch (frame->code->instructions[frame->ip]) {
        case ST_VM_OP_GETGLOBAL: {
            ST_Object gVarSymb = frame->code->symbTab[ST_readLE16(frame, 1)];
            ST_Object global = ST_getGlobal(context, gVarSymb);
            ST_pushStack(context, global);
            frame->ip += 1 + sizeof(ST_LE16);
        } break;

        case ST_VM_OP_SETGLOBAL: {
            ST_Object gVarSymb = frame->code->symbTab[ST_readLE16(frame, 1)];
            ST_setGlobal(context, gVarSymb, ST_refStack(context, 0));
            ST_popStack(context);
            frame->ip += 1 + sizeof(ST_LE16);
        } break;

        case ST_VM_OP_PUSHNIL:
            ST_pushStack(context, ST_getNilValue(context));
            frame->ip += 1;
            break;

        case ST_VM_OP_PUSHTRUE:
            ST_pushStack(context, ST_getTrueValue(context));
            frame->ip += 1;
            break;

        case ST_VM_OP_PUSHFALSE:
            ST_pushStack(context, ST_getFalseValue(context));
            frame->ip += 1;
            break;

        case ST_VM_OP_PUSHSYMBOL:
            ST_pushStack(context, frame->code->symbTab[ST_readLE16(frame, 1)]);
            frame->ip += 1 + sizeof(ST_LE16);
            break;

        case ST_VM_OP_SENDMESSAGE: {
            const ST_Object selector =
                frame->code->symbTab[ST_readLE16(frame, 1)];
            ST_Object receiver = ST_refStack(context, 0);
            ST_Internal_Method *method =
                ST_Internal_Object_getMethod(context, receiver, selector);
            frame->ip += 1 + sizeof(ST_LE16);
            if (method) {
                switch (method->type) {
                case ST_METHOD_TYPE_FOREIGN:
                    ST_popStack(context); /* pop receiver */
                    ST_VM_invokeForeignMethod_NArg(context, receiver, method);
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
            const ST_Byte argc = frame->code->instructions[frame->ip + 3];
            ST_MethodMap_Entry *entry =
                ST_alloc(context, sizeof(ST_MethodMap_Entry));
            ST_BST_Node_init((ST_BST_Node *)entry);
            frame->ip += 3;
            entry->header.symbol = selector;
            entry->method.type = ST_METHOD_TYPE_COMPILED;
            entry->method.argc = argc;
            entry->method.payload.compiledMethod.source = frame->code;
            entry->method.payload.compiledMethod.offset =
                frame->ip + sizeof(ST_LE32) + 1;
            if (!ST_Class_insertMethodEntry(context, target, entry)) {
                ST_free(context, entry);
            }
            ST_popStack(context);
            frame->ip += ST_readLE32(frame, 1) + sizeof(ST_LE32) + 1;
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
        } break;

        case ST_VM_OP_DUP: {
            ST_Object top = ST_refStack(context, 0);
            ST_pushStack(context, top);
            ++frame->ip;
        } break;

        case ST_VM_OP_POP: {
            ST_popStack(context);
            ++frame->ip;
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
// GC
/////////////////////////////////////////////////////////////////////////////*/

/* TODO */

/*//////////////////////////////////////////////////////////////////////////////
// Language types and methods
/////////////////////////////////////////////////////////////////////////////*/

static ST_Object ST_new(ST_Context context, ST_Object self, ST_Object argv[]) {
    ST_Class *class = self;
    ST_Internal_Object *instance = ST_Pool_alloc(context, &class->instancePool);
    ST_Internal_Object **ivars = ST_Object_getIVars(instance);
    ST_Size i;
    for (i = 0; i < class->instanceVariableCount; ++i) {
        ivars[i] = ST_getNilValue(context);
    }
    instance->class = class;
    return instance;
}

/* TODO: make subclass accept instanceVariableNames as a param so we know
   what element size to use when creating the instance pool */
static ST_Object ST_subclass(ST_Context context, ST_Object self,
                             ST_Object argv[]) {
    ST_Class *sub = ST_alloc(context, sizeof(ST_Class));
    sub->object.class = sub;
    sub->super = self; /* Note: receiver is a class */
    sub->instanceVariableCount = ((ST_Class *)self)->instanceVariableCount;
    sub->methodTree = NULL;
    ST_Pool_init(context, &sub->instancePool,
                 sizeof(ST_Internal_Object) +
                     sizeof(ST_Internal_Object *) * sub->instanceVariableCount,
                 100 /* FIXME: better initial size? */);
    return sub;
}

static ST_Object ST_superclass(ST_Context context, ST_Object self,
                               ST_Object argv[]) {
    return ((ST_Internal_Object *)self)->class->super;
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
    ST_SETMETHOD(context, "Object", "doesNotUnderstand", ST_doesNotUnderstand,
                 1);
    ST_SUBCLASS(context, "Object", "Message");
}

static bool ST_Internal_Context_bootstrap(ST_Internal_Context *context) {
    /* We need to do things manually for a bit, until we've defined the
     symbol class and the new method, because most of the functions in
     the runtime depend on Symbol. */
    ST_Class *cObject = ST_alloc(context, sizeof(ST_Class));
    ST_Class *cSymbol = ST_alloc(context, sizeof(ST_Class));
    ST_Internal_Object *symbolSymbol =
        ST_alloc(context, sizeof(ST_Internal_Object));
    ST_Internal_Object *newSymbol =
        ST_alloc(context, sizeof(ST_Internal_Object));
    ST_StringMap_Entry *newEntry =
        ST_alloc(context, sizeof(ST_StringMap_Entry));
    cObject->object.class = cObject;
    cObject->super = cObject;
    cObject->methodTree = NULL;
    cObject->instanceVariableCount = 0;
    ST_Pool_init(context, &cObject->instancePool, sizeof(ST_Object), 100);
    cSymbol->super = cObject;
    cSymbol->object.class = cSymbol;
    cSymbol->methodTree = NULL;
    cSymbol->instanceVariableCount = 0;
    ST_Pool_init(context, &cSymbol->instancePool, sizeof(ST_Object), 512);
    symbolSymbol->class = cSymbol;
    newSymbol->class = cSymbol;
    context->symbolRegistry = ST_alloc(context, sizeof(ST_StringMap_Entry));
    ST_BST_Node_init((ST_BST_Node *)context->symbolRegistry);
    context->symbolRegistry->key = ST_strdup(context, "Symbol");
    context->symbolRegistry->value = symbolSymbol;
    context->globalScope = ST_alloc(context, sizeof(ST_GlobalVarMap_Entry));
    ST_BST_Node_init((ST_BST_Node *)context->globalScope);
    context->globalScope->header.symbol = symbolSymbol;
    context->globalScope->value = (ST_Object)cSymbol;
    newEntry->key = ST_strdup(context, "new");
    newEntry->value = newSymbol;
    ST_BST_insert((ST_BST_Node **)&context->symbolRegistry,
                  &newEntry->nodeHeader, ST_StringMap_comparator);
    ST_Object_setMethod(context, cObject, newSymbol, ST_new, 0);
    ST_setGlobal(context, ST_requestSymbol(context, "Object"), cObject);
    return true;
}

static ST_Object ST_ifTrueImplForTrue(ST_Context context, ST_Object self,
                                      ST_Object argv[]) {
    ST_Object valueSymbol = ST_requestSymbol(context, "value");
    return ST_Object_sendMessage(context, argv[0], valueSymbol, 0, NULL);
}

static ST_Object ST_ifFalseImplForFalse(ST_Context context, ST_Object self,
                                        ST_Object argv[]) {
    ST_Object valueSymbol = ST_requestSymbol(context, "value");
    return ST_Object_sendMessage(context, argv[0], valueSymbol, 0, NULL);
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
    ST_SETMETHOD(context, "True", "ifTrue", ST_ifTrueImplForTrue, 1);
    ST_SETMETHOD(context, "True", "ifFalse", ST_nopMethod, 1);
    ST_SETMETHOD(context, "False", "ifFalse", ST_ifFalseImplForFalse, 1);
    ST_SETMETHOD(context, "False", "ifTrue", ST_nopMethod, 1);
    context->trueValue = ST_NEW(context, "True");
    context->falseValue = ST_NEW(context, "False");
}

ST_Context ST_createContext(const ST_Context_Configuration *config) {
    ST_Internal_Context *context =
        config->memory.allocFn(sizeof(ST_Internal_Context));
    if (!context)
        return NULL;
    context->config = *config;
    ST_Pool_init(context, &context->gvarNodePool, sizeof(ST_GlobalVarMap_Entry),
                 1000);
    ST_Pool_init(context, &context->vmFramePool, sizeof(ST_VM_Frame), 50);
    ST_Internal_Context_bootstrap(context);
    ST_Vector_init(context, &context->operandStack,
                   sizeof(ST_Internal_Object *), 1024);
    ST_SETMETHOD(context, "Object", "subclass", ST_subclass, 0);
    ST_SETMETHOD(context, "Object", "superclass", ST_superclass, 0);
    ST_SETMETHOD(context, "Object", "class", ST_class, 0);
    ST_initNil(context);
    ST_initBoolean(context);
    ST_Internal_Context_initErrorHandling(context);
    return context;
}
