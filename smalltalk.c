#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "smalltalk.h"

void ST_Internal_Context_pushStack(ST_Context context, ST_Object val);
void ST_Internal_Context_popStack(ST_Context context);
ST_Object ST_Internal_Context_refStack(ST_Context context, ST_Size offset);

/*//////////////////////////////////////////////////////////////////////////////
// Helper functions
/////////////////////////////////////////////////////////////////////////////*/

typedef enum { false, true } bool;

static void ST_Internal_fatalError(ST_Context context, const char* error)
{
    puts(error);
    exit(EXIT_FAILURE);
}

typedef enum ST_Cmp {
    ST_Cmp_Greater = 1,
    ST_Cmp_Less = -1,
    ST_Cmp_Eq = 0
} ST_Cmp;

static inline char* ST_Internal_strdup(const char *s)
{
    char *d = malloc (strlen(s) + 1);
    if (d == NULL) return NULL;
    strcpy(d, s);
    return d;
}

static inline int ST_Internal_clamp(int value, int low, int high)
{
    if (value < low) return low;
    else if (value > high) return high;
    else return value;
}

/*//////////////////////////////////////////////////////////////////////////////
// Search Tree (Intrusive BST)
/////////////////////////////////////////////////////////////////////////////*/

typedef struct ST_Internal_BST_Node {
    struct ST_Internal_BST_Node* left;
    struct ST_Internal_BST_Node* right;
} ST_Internal_BST_Node;

static void ST_Internal_BST_Node_init(ST_Internal_BST_Node* node)
{
    node->left = NULL;
    node->right = NULL;
}

static void ST_Internal_BST_splay(ST_Internal_BST_Node** t,
                                  void* key,
                                  ST_Cmp (*comparator)(void*, void*))
{
    ST_Internal_BST_Node N, *l, *r, *y;
    N.left = N.right = NULL;
    l = r = &N;
    while (true) {
        const ST_Cmp compareResult = comparator(*t, key);
        if (compareResult == ST_Cmp_Greater) {
	    if ((*t)->left != NULL && comparator((*t)->left, key) == ST_Cmp_Greater) {
		y = (*t)->left; (*t)->left = y->right; y->right = *t; *t = y;
	    }
	    if ((*t)->left == NULL) break;
	    r->left = *t; r = *t; *t = (*t)->left;
	} else if (compareResult == ST_Cmp_Less) {
	    if ((*t)->right != NULL && comparator((*t)->right, key) == ST_Cmp_Less) {
		y = (*t)->right; (*t)->right = y->left; y->left = *t; *t = y;
	    }
	    if ((*t)->right == NULL) break;
	    l->right = *t; l = *t; *t = (*t)->right;
	} else break;
    }
    l->right = (*t)->left;
    r->left = (*t)->right;
    (*t)->left = N.right;
    (*t)->right = N.left;
}

static bool ST_Internal_BST_insert(ST_Internal_BST_Node** root,
                                   ST_Internal_BST_Node* node,
                                   ST_Cmp (*comparator)(void*, void*))
{
    ST_Internal_BST_Node* current = *root;
    if (!*root) {
        *root = node;
        return true;
    }
    while (true) {
        switch (comparator(current, node)) {
        case ST_Cmp_Eq: /* (Key exists) */ return false;
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

static ST_Internal_BST_Node* ST_Internal_BST_find(ST_Internal_BST_Node** root,
                                                  void* key,
                                                  ST_Cmp (*comp)(void*, void*))
{
    ST_Internal_BST_Node* current = *root;
    if (!*root) return NULL;
    while (true) {
        switch (comp(current, key)) {
        case ST_Cmp_Eq: /* (found) */
            return current;
        case ST_Cmp_Greater:
            if (current->left) {
                current = current->left;
                break;
            } else return NULL;
        case ST_Cmp_Less:
            if (current->right) {
                current = current->right;
                break;
            } else return NULL;
        }
    }
}

/*//////////////////////////////////////////////////////////////////////////////
// Symbol Map
/////////////////////////////////////////////////////////////////////////////*/

typedef struct ST_Internal_SymbolMap_Entry {
    ST_Internal_BST_Node node;
    ST_Object symbol;
} ST_Internal_SymbolMap_Entry;

static ST_Cmp ST_Internal_SymbolMap_insertComparator(void* left,
                                                     void* right)
{
    ST_Internal_SymbolMap_Entry* leftEntry = left;
    ST_Internal_SymbolMap_Entry* rightEntry = right;
    if (leftEntry->symbol > rightEntry->symbol) {
        return ST_Cmp_Less;
    } else if (leftEntry->symbol < rightEntry->symbol) {
        return ST_Cmp_Greater;
    } else {
        return ST_Cmp_Eq;
    }
}

static ST_Cmp ST_Internal_SymbolMap_findComparator(void* left,
                                                   void* right)
{
    ST_Internal_SymbolMap_Entry* entry = left;
    ST_Object* symbol = right;
    if (*symbol < entry->symbol) {
        return ST_Cmp_Less;
    } else if (*symbol > entry->symbol) {
        return ST_Cmp_Greater;
    } else {
        return ST_Cmp_Eq;
    }
}

/*//////////////////////////////////////////////////////////////////////////////
// Method
/////////////////////////////////////////////////////////////////////////////*/

typedef ST_Method ST_Internal_ForeignMethod;
typedef ST_CodeBlock ST_Internal_CompiledMethod;

typedef struct ST_Internal_Method {
    enum {
        ST_METHOD_TYPE_FOREIGN,
        ST_METHOD_TYPE_COMPILED
    } type;
    union {
        ST_Internal_ForeignMethod foreignMethod;
        ST_Internal_CompiledMethod compiledMethod;
    } payload;
    ST_Size argc;
} ST_Internal_Method;

/*//////////////////////////////////////////////////////////////////////////////
// Object
/////////////////////////////////////////////////////////////////////////////*/

typedef struct ST_Internal_Object {
    struct ST_Internal_MethodMap_Entry* methodTree;
    struct ST_Internal_Object* class;
    struct ST_Internal_Object* super;
    struct ST_Internal_Object** instanceVariables;
    ST_Size instanceVariableCount;
} ST_Internal_Object;

typedef struct ST_Internal_MethodMap_Entry {
    ST_Internal_SymbolMap_Entry header;
    ST_Internal_Method method;
} ST_Internal_MethodMap_Entry;

ST_Internal_Method* ST_Internal_Object_getMethod(ST_Context context,
                                                 ST_Internal_Object* obj,
                                                 ST_Internal_Object* selector)
{
    ST_Internal_Object* currentClass = obj->class;
    while (true) {
        ST_Internal_BST_Node* entry =
            ST_Internal_BST_find((ST_Internal_BST_Node**)
                                 &currentClass->methodTree,
                                 &selector,
                                 ST_Internal_SymbolMap_findComparator);
        if (entry) {
            return &((ST_Internal_MethodMap_Entry*)entry)->method;
        } else {
            /* Note: the current dummy implementation of metaclasses works by having a class
               hold a circular reference to itself, so we need to test self/super-equality
               before walking up the meta-class hierarchy. */
            if (currentClass->super != currentClass) {
                currentClass = currentClass->super;
            } else {
                return NULL;
            }
        }
    }
}

static void ST_Internal_failedMethodLookup(ST_Context context,
                                           ST_Object receiver,
                                           ST_Object selector)
{
    ST_Object err = ST_NEW(context, "MessageNotUnderstood");
    ST_Object_sendMessage(context,
                          receiver,
                          ST_Context_requestSymbol(context, "doesNotUnderstand"),
                          1, &err);
}

ST_Object ST_Object_sendMessage(ST_Context context,
                                ST_Object receiver,
                                ST_Object selector,
                                ST_Size argc,
                                ST_Object argv[])
{
    ST_Internal_Method* method =
        ST_Internal_Object_getMethod(context, receiver, selector);
    if (method) {
        switch (method->type) {
        case ST_METHOD_TYPE_FOREIGN:
            assert(argc == method->argc && "wrong number of args");
            return method->payload.foreignMethod(context, receiver, argv);

        case ST_METHOD_TYPE_COMPILED: {
            ST_Size i;
            ST_Object result;
            for (i = 0; i < argc; ++i) {
                ST_Internal_Context_pushStack(context, argv[i]);
            }
            ST_VM_eval(context, &method->payload.compiledMethod);
            result = ST_Internal_Context_refStack(context, 0);
            ST_Internal_Context_popStack(context); /* Pop result */
            for (i = 0; i < argc; ++i) {
                ST_Internal_Context_popStack(context); /* Need to pop argv too */
            }
            return result;
        }
        }
    }
    ST_Internal_failedMethodLookup(context, receiver, selector);
    return ST_Context_getNilValue(context);
}

void ST_Object_setMethod(ST_Context context,
                         ST_Object object,
                         ST_Object selector,
                         ST_Internal_ForeignMethod foreignMethod,
                         ST_Size argc)
{
    ST_Internal_MethodMap_Entry* entry =
        malloc(sizeof(ST_Internal_MethodMap_Entry));
    if (!entry) ST_Internal_fatalError(context, "bad malloc");
    ST_Internal_BST_Node_init(&entry->header.node);
    entry->header.symbol = selector;
    entry->method.type = ST_METHOD_TYPE_FOREIGN;
    entry->method.payload.foreignMethod = foreignMethod;
    entry->method.argc = argc;
    if (!ST_Internal_BST_insert((ST_Internal_BST_Node**)
                                &((ST_Internal_Object*)object)->methodTree,
                                &entry->header.node,
                                ST_Internal_SymbolMap_insertComparator)) {
        free(entry);
        return;
    }
    ST_Internal_BST_splay((ST_Internal_BST_Node**)
                          &((ST_Internal_Object*)object)->methodTree,
                          &entry->header.node,
                          ST_Internal_SymbolMap_insertComparator);
}

ST_Object ST_Object_getSuper(ST_Context context, ST_Object object)
{
    return ((ST_Internal_Object*)object)->super;
}

ST_Object ST_Object_getClass(ST_Context context, ST_Object object)
{
    return ((ST_Internal_Object*)object)->class;
}

ST_Object ST_Object_getInstanceVar(ST_Context context,
                                   ST_Object object,
                                   ST_Size position)
{
    ST_Internal_Object* obj = object;
    if (position >= obj->instanceVariableCount) {
        ST_Internal_fatalError(context, "Instance variable index out of bounds");
    }
    return obj->instanceVariables[position];
}

void ST_Object_setInstanceVar(ST_Context context,
                              ST_Object object,
                              ST_Size position,
                              ST_Object value)
{
    ST_Internal_Object* obj = object;
    if (position >= obj->instanceVariableCount) {
        ST_Internal_fatalError(context, "Instance variable index out of bounds");
    }
    obj->instanceVariables[position] = value;
}

/*//////////////////////////////////////////////////////////////////////////////
// Vector
/////////////////////////////////////////////////////////////////////////////*/

typedef struct ST_Internal_Vector {
    char* data;
    ST_Size len;
    ST_Size npos;
    ST_Size elemSize;
} ST_Internal_Vector;

int ST_Internal_Vector_init(ST_Internal_Vector* vec, ST_Size elemSize)
{
    vec->data = calloc(1, elemSize);
    if (!vec->data) return 0;
    vec->len = elemSize;
    vec->npos = 0;
    vec->elemSize = elemSize;
    return 1;
}

int ST_Internal_Vector_push(ST_Internal_Vector* vec, const void* element)
{
    const size_t elemSize = vec->elemSize;
    const int growthRate = 2;
    if (vec->len >= (vec->npos + 1) * elemSize) {
        memcpy(vec->data + vec->npos * elemSize, element, elemSize);
        vec->npos += 1;
    } else {
        char * newData = malloc(vec->len * growthRate);
        if (!newData) return 0;
        memcpy(newData, vec->data, vec->len);
        free(vec->data);
        vec->data = newData;
        memcpy(vec->data + vec->npos * elemSize, element, elemSize);
        vec->npos += 1;
        vec->len *= growthRate;
    }
    return 1;
}

void* ST_Internal_Vector_pop(ST_Internal_Vector* vec)
{
    if (vec->npos) {
        vec->npos -= 1;
        return vec->data + vec->npos * vec->elemSize;
    }
    return NULL;
}

void* ST_Internal_Vector_get(ST_Internal_Vector* vec, ST_Size index)
{
    return vec->data + index * vec->elemSize;
}

void* ST_Internal_Vector_back(ST_Internal_Vector* vec)
{
    if (vec->npos -= 0) {
        return NULL;
    }
    return vec->data + (vec->npos - 1) * vec->elemSize;
}

void ST_Internal_Vector_free(ST_Internal_Vector* vec)
{
    vec->npos = 0;
    vec->elemSize = 0;
    vec->len = 0;
    free(vec->data);
}

/*//////////////////////////////////////////////////////////////////////////////
// Context
/////////////////////////////////////////////////////////////////////////////*/

typedef struct ST_Internal_Context {
    struct ST_Internal_StringMap_Entry* symbolRegistry;
    struct ST_Internal_GlobalVarMap_Entry* globalScope;
    ST_Internal_Object* nilValue;
    ST_Internal_Object* trueValue;
    ST_Internal_Object* falseValue;
    ST_Internal_Vector operandStack;
} ST_Internal_Context;

typedef struct ST_Internal_StringMap_Entry {
    ST_Internal_BST_Node nodeHeader;
    char* key;
    void* value;
} ST_Internal_StringMap_Entry;

ST_Cmp ST_Internal_StringMap_findComparator(void* left, void* right)
{
    ST_Internal_StringMap_Entry* entry = left;
    const char* searchKey = right;
    return ST_Internal_clamp(strcmp(entry->key, searchKey),
                             ST_Cmp_Less, ST_Cmp_Greater);
}

ST_Cmp ST_Internal_StringMap_insertComparator(void* left, void* right)
{
    ST_Internal_StringMap_Entry* lhsEntry = left;
    ST_Internal_StringMap_Entry* rhsEntry = right;
    return ST_Internal_clamp(strcmp(lhsEntry->key, rhsEntry->key),
                             ST_Cmp_Less, ST_Cmp_Greater);
}

typedef struct ST_Internal_GlobalVarMap_Entry {
    ST_Internal_SymbolMap_Entry header;
    ST_Internal_Object* value;
} ST_Internal_GlobalVarMap_Entry;

void ST_Internal_Context_pushStack(ST_Context context, ST_Object val)
{
    ST_Internal_Vector_push(&((ST_Internal_Context*)context)->operandStack,
                            &val);
}

void ST_Internal_Context_popStack(ST_Context context)
{
    ST_Internal_Vector_pop(&((ST_Internal_Context*)context)->operandStack);
}

ST_Object ST_Internal_Context_refStack(ST_Context context, ST_Size offset)
{
    ST_Internal_Vector* stack = &((ST_Internal_Context*)context)->operandStack;
    void* result = ST_Internal_Vector_get(stack, stack->npos - (offset + 1));
    if (!result) {
        return ST_Context_getNilValue(context);
    }
    return *(ST_Internal_Object**)result;
}

ST_Object ST_Context_getGlobal(ST_Context context, ST_Object symbol)
{
    ST_Internal_BST_Node* found;
    ST_Internal_BST_Node** globalScope =
        (void*)&((ST_Internal_Context*)context)->globalScope;
    found = ST_Internal_BST_find(globalScope, &symbol,
                                 ST_Internal_SymbolMap_findComparator);
    if (!found) {
        printf("warning: global variable \"%s\" not found\n",
               ST_Symbol_toString(context, symbol));
        return ST_Context_getNilValue(context);
    }
    ST_Internal_BST_splay(globalScope, &symbol,
                          ST_Internal_SymbolMap_findComparator);
    return ((ST_Internal_GlobalVarMap_Entry*)found)->value;
}

void ST_Context_setGlobal(ST_Context context, ST_Object symbol, ST_Object object)
{
    ST_Internal_Context* extCtx = context;
    ST_Internal_GlobalVarMap_Entry* entry =
        malloc(sizeof(ST_Internal_GlobalVarMap_Entry));
    if (!entry) ST_Internal_fatalError(context, "bad malloc");
    ST_Internal_BST_Node_init(&entry->header.node);
    entry->header.symbol = symbol;
    entry->value = object;
    if (!ST_Internal_BST_insert((ST_Internal_BST_Node**)
                                &extCtx->globalScope,
                                &entry->header.node,
                                ST_Internal_SymbolMap_insertComparator)) {
        free(entry);
    }
}

ST_Object ST_Context_getNilValue(ST_Context context)
{
    return ((ST_Internal_Context*)context)->nilValue;
}

ST_Object ST_Context_getTrueValue(ST_Context context)
{
    return ((ST_Internal_Context*)context)->trueValue;
}

ST_Object ST_Context_getFalseValue(ST_Context context)
{
    return ((ST_Internal_Context*)context)->falseValue;
}

ST_Object ST_Context_requestSymbol(ST_Context context,
                                   const char* symbolName)
{
    ST_Internal_Context* extCtx = context;
    ST_Internal_BST_Node* found =
        ST_Internal_BST_find((ST_Internal_BST_Node**)
                             &extCtx->symbolRegistry,
                             (void*)symbolName,
                             ST_Internal_StringMap_findComparator);
    ST_Internal_StringMap_Entry* newEntry = NULL;
    if (found) {
        return (ST_Object)((ST_Internal_StringMap_Entry*)found)->value;
    }
    newEntry = malloc(sizeof(ST_Internal_StringMap_Entry));
    if (!newEntry) ST_Internal_fatalError(context, "bad malloc");
    ST_Internal_BST_Node_init(&newEntry->nodeHeader);
    newEntry->key = ST_Internal_strdup(symbolName);
    if (!newEntry->key) ST_Internal_fatalError(context, "bad malloc");
    newEntry->value = ST_NEW(context, "Symbol");
    if (!ST_Internal_BST_insert((ST_Internal_BST_Node**)
                                &extCtx->symbolRegistry,
                                &newEntry->nodeHeader,
                                ST_Internal_StringMap_insertComparator)) {
        free(newEntry->key);
        free(newEntry);
        return ST_Context_getNilValue(context);
    }
    return newEntry->value;
}

const char* ST_Internal_recDecodeSymbol(ST_Internal_StringMap_Entry* tree,
                                        ST_Object symbol)
{
    if ((ST_Object)tree->value == symbol) return tree->key;
    if (tree->nodeHeader.left) {
        const char* found =
            ST_Internal_recDecodeSymbol(((ST_Internal_StringMap_Entry*)
                                         tree->nodeHeader.left), symbol);
        if (found) return found;
    }
    if (tree->nodeHeader.right) {
        const char* found =
            ST_Internal_recDecodeSymbol(((ST_Internal_StringMap_Entry*)
                                         tree->nodeHeader.right), symbol);
        if (found) return found;
    }
    return NULL;
}

const char* ST_Symbol_toString(ST_Context context, ST_Object symbol)
{
    ST_Internal_Context* ctx = context;
    ST_Internal_StringMap_Entry* symbolRegistry = ctx->symbolRegistry;
    if (symbolRegistry) {
        return ST_Internal_recDecodeSymbol(symbolRegistry, symbol);
    }
    return NULL;
}

/*//////////////////////////////////////////////////////////////////////////////
// VM
/////////////////////////////////////////////////////////////////////////////*/

#include "opcode.h"

void ST_Internal_VM_invokeCompiledMethod(ST_Internal_Context* context,
                                         ST_Size* ip)
{
    assert(!"Compiled methods unimplemented");
    /* TODO: store ip, store context, set context to method's bc, continue
       evaluation. */
}

typedef void (*ST_Internal_VM_FFIHandler)(ST_Internal_Context* context,
                                          ST_Object receiver,
                                          ST_Internal_Method* method);

void ST_Internal_VM_invokeForeignMethod_NArg(ST_Internal_Context* context,
                                             ST_Object receiver,
                                             ST_Internal_Method* method)
{
    ST_Object* argv = malloc(sizeof(ST_Object) * method->argc);
    ST_Size i;
    for (i = 0; i < method->argc; ++i) {
        argv[i] = ST_Internal_Context_refStack(context, 0);
        ST_Internal_Context_popStack(context);
    }
    ST_Internal_Context_pushStack(context,
                                  method->payload.foreignMethod(context,
                                                                receiver,
                                                                argv));
    free(argv);
}

void ST_Internal_VM_invokeForeignMethod_1Arg(ST_Internal_Context* context,
                                             ST_Object receiver,
                                             ST_Internal_Method* method)
{
    ST_Object arg = ST_Internal_Context_refStack(context, 0);
    ST_Internal_Context_popStack(context);
    ST_Internal_Context_pushStack(context,
                                  method->payload.foreignMethod(context,
                                                                receiver,
                                                                &arg));
}

void ST_Internal_VM_invokeForeignMethod_0Arg(ST_Internal_Context* context,
                                             ST_Object receiver,
                                             ST_Internal_Method* method)
{
    ST_Internal_Context_pushStack(context,
                                  method->payload.foreignMethod(context,
                                                                receiver,
                                                                NULL));
}

void ST_Internal_VM_sendMessage(ST_Internal_Context* context,
                                ST_Size* ip,
                                ST_Object selector,
                                ST_Internal_VM_FFIHandler ffiHandler)
{
    ST_Object receiver = ST_Internal_Context_refStack(context, 0);
    ST_Internal_Method* method =
        ST_Internal_Object_getMethod(context, receiver, selector);
    if (method) {
        switch (method->type) {
        case ST_METHOD_TYPE_FOREIGN:
            ST_Internal_Context_popStack(context); /* Pop receiver */
            ffiHandler(context, receiver, method);
            break;

        case ST_METHOD_TYPE_COMPILED:
            ST_Internal_VM_invokeCompiledMethod(context, ip);
            break;
        }
    } else {
        ST_Internal_failedMethodLookup(context, receiver, selector);
    }
}

typedef unsigned short ST_SymbolTable_Index;

void ST_VM_eval(ST_Context context, ST_CodeBlock* code)
{
#define READ_LE_16() ((unsigned short)imem[ip+1])|((unsigned short)imem[ip+2]<<8)
    ST_Size ip = 0;
    const ST_Byte* imem = code->instructions;
    while (ip < code->length) {
        switch (imem[ip]) {
        case ST_VM_OP_GETGLOBAL: {
            ST_Object globalVarSymb = code->symbolTable[READ_LE_16()];
            ST_Object global = ST_Context_getGlobal(context, globalVarSymb);
            ST_Internal_Context_pushStack(context, global);
            ip += 1 + sizeof(ST_SymbolTable_Index);
        } break;

        case ST_VM_OP_SETGLOBAL: {
            ST_Object globalVarSymb = code->symbolTable[READ_LE_16()];
            ST_Context_setGlobal(context, globalVarSymb,
                                 ST_Internal_Context_refStack(context, 0));
            ST_Internal_Context_popStack(context);
            ip += 1 + sizeof(ST_SymbolTable_Index);
        } break;

        case ST_VM_OP_PUSHNIL:
            ST_Internal_Context_pushStack(context, ST_Context_getNilValue(context));
            ip += 1;
            break;

        case ST_VM_OP_PUSHTRUE:
            ST_Internal_Context_pushStack(context, ST_Context_getTrueValue(context));
            ip += 1;
            break;

        case ST_VM_OP_PUSHFALSE:
            ST_Internal_Context_pushStack(context, ST_Context_getFalseValue(context));
            ip += 1;
            break;

        case ST_VM_OP_SENDMESSAGE: {
            const ST_Object selector = code->symbolTable[READ_LE_16()];
            ip += 1 + sizeof(ST_SymbolTable_Index);
            ST_Internal_VM_sendMessage(context, &ip, selector,
                                       ST_Internal_VM_invokeForeignMethod_NArg);
        } break;

        case ST_VM_OP_SENDMESSAGE0: {
            const ST_Object selector = code->symbolTable[READ_LE_16()];
            ip += 1 + sizeof(ST_SymbolTable_Index);
            ST_Internal_VM_sendMessage(context, &ip, selector,
                                       ST_Internal_VM_invokeForeignMethod_0Arg);
        } break;

        case ST_VM_OP_SENDMESSAGE1: {
            ip += 1 + sizeof(ST_SymbolTable_Index);
            const ST_Object selector = code->symbolTable[READ_LE_16()];
            ST_Internal_VM_sendMessage(context, &ip, selector,
                                       ST_Internal_VM_invokeForeignMethod_1Arg);
        } break;

        default:
            printf("evaluation failure: invalid bytecode: %d\n", imem[ip]);
            exit(EXIT_FAILURE);
        }
    }
#undef READ_LE_16
}

/*//////////////////////////////////////////////////////////////////////////////
// GC
/////////////////////////////////////////////////////////////////////////////*/

/* TODO */

/*//////////////////////////////////////////////////////////////////////////////
// Language types and methods
/////////////////////////////////////////////////////////////////////////////*/

static ST_Object ST_Internal_allocateObject(ST_Context context,
                                            ST_Object self,
                                            ST_Object argv[])
{
    ST_Internal_Object* obj = malloc(sizeof(ST_Internal_Object));
    if (!obj) ST_Internal_fatalError(context, "bad malloc");
    obj->methodTree = NULL;
    /* Note: because the receiver of a 'new' message will always be a class (or metaclass) */
    /* But, still, TODO: implement real metaclasses. */
    obj->class = ((ST_Internal_Object*)self)->class;
    obj->super = obj->class->super;
    return obj;
}

static ST_Object ST_Internal_subclass(ST_Context context,
                                      ST_Object self,
                                      ST_Object argv[])
{
    ST_Object newSymb;
    ST_Internal_Object* sub;
    newSymb = ST_Context_requestSymbol(context, "new");
    sub = ST_Internal_allocateObject(context, self, NULL);
    sub->class = sub;
    sub->super = ((ST_Internal_Object*)self)->class;
    ST_Object_setMethod(context, sub, newSymb, ST_Internal_allocateObject, 0);
    return sub;
}

static ST_Object ST_Internal_doesNotUnderstand(ST_Context context,
                                               ST_Object self,
                                               ST_Object argv[])
{
    puts("TODO: message not understood...");
    return ST_Context_getNilValue(context);
}

static ST_Object ST_Internal_defineInstanceVariables(ST_Context context,
                                                     ST_Object self,
                                                     ST_Object argv[])
{
    if (((ST_Internal_Object*)self)->instanceVariables) {
        ST_Internal_fatalError(context,
                               "Receiver class already has instance variables."
                               " The implementation does not yet support "
                               "redefinition of instance variables.");
    }
    ST_Internal_fatalError(context, "instanceVariableNames unimplemented");
    return ST_Context_getNilValue(context);
}

static void ST_Internal_initNil(ST_Internal_Context* context)
{
    ST_SUBCLASS(context, "Object", "UndefinedObject");
    context->nilValue = ST_NEW(context, "UndefinedObject");
}

static void ST_Internal_initBoolean(ST_Internal_Context* context)
{
    ST_SUBCLASS(context, "Object", "Boolean");
    ST_SUBCLASS(context, "Boolean", "True");
    ST_SUBCLASS(context, "Boolean", "False");
    context->trueValue = ST_NEW(context, "True");
    context->falseValue = ST_NEW(context, "False");
}

static void ST_Internal_initErrorHandling(ST_Internal_Context* context)
{
    ST_SUBCLASS(context, "Object", "MessageNotUnderstood");
    ST_SETMETHOD(context, "Object", "doesNotUnderstand",
                 ST_Internal_doesNotUnderstand, 1);
    ST_SUBCLASS(context, "Object", "Message");
}

void ST_Internal_Context_bootstrap(ST_Internal_Context* context)
{
    /* We need to do things manually for a bit, until we've defined the
       symbol class and the new method, because most of the core functions
       depend on Symbol. */
    ST_Internal_Object* cObject = calloc(1, sizeof(ST_Internal_Object));
    ST_Internal_Object* cSymbol = calloc(1, sizeof(ST_Internal_Object));
    ST_Internal_Object* symbolSymbol = calloc(1, sizeof(ST_Internal_Object));
    ST_Internal_Object* newSymbol = calloc(1, sizeof(ST_Internal_Object));
    ST_Internal_StringMap_Entry* newEntry =
            calloc(1, sizeof(ST_Internal_StringMap_Entry));
    assert(cObject && cSymbol && symbolSymbol && newSymbol && newEntry);
    cObject->class = cObject;
    cObject->super = cObject;
    cSymbol->super = cObject;
    cSymbol->class = cSymbol;
    symbolSymbol->class = cSymbol;
    newSymbol->class = cSymbol;
    context->symbolRegistry = calloc(1, sizeof(ST_Internal_StringMap_Entry));
    context->symbolRegistry->key = ST_Internal_strdup("Symbol");
    context->symbolRegistry->value = symbolSymbol;
    context->globalScope = calloc(1, sizeof(ST_Internal_GlobalVarMap_Entry));
    context->globalScope->header.symbol = symbolSymbol;
    context->globalScope->value = cSymbol;
    assert(ST_Context_requestSymbol(context, "Symbol") == symbolSymbol);
    newEntry->key = ST_Internal_strdup("new");
    newEntry->value = newSymbol;
    ST_Internal_BST_insert((ST_Internal_BST_Node**)&context->symbolRegistry,
                           &newEntry->nodeHeader,
                           ST_Internal_StringMap_insertComparator);
    assert(ST_Context_requestSymbol(context, "new") == newSymbol);
    ST_Object_setMethod(context, cObject, newSymbol,
                        ST_Internal_allocateObject, 0);
    ST_Context_setGlobal(context, ST_Context_requestSymbol(context, "Object"), cObject);
}

ST_Context ST_Context_create()
{
    ST_Internal_Context* context = malloc(sizeof(ST_Internal_Context));
    if (!context) ST_Internal_fatalError(context, "bad malloc");
    ST_Internal_Context_bootstrap(context);
    ST_Internal_Vector_init(&context->operandStack, sizeof(ST_Internal_Object*));
    ST_SETMETHOD(context, "Object", "subclass", ST_Internal_subclass, 0);
    ST_SETMETHOD(context, "Object", "instanceVariableNames",
                 ST_Internal_defineInstanceVariables, 1);
    ST_Internal_initNil(context);
    ST_Internal_initBoolean(context);
    ST_Internal_initErrorHandling(context);
    return context;
}
