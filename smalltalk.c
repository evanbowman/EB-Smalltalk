#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "smalltalk.h"


enum ST_Cmp {
    ST_Cmp_Greater = 1,
    ST_Cmp_Less = -1,
    ST_Cmp_Eq = 0
};


static const ST_Symbol ST_UnknownSymbol = 0;


#define ST_UNUSED(VAR) ((void)VAR)


static char* ST_Internal_strdup(const char *s)
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


static void ST_Internal_raiseError(ST_Image image,
                                   ST_Object receiver,
                                   ST_Symbol selector,
                                   const char* error)
{
    ST_UNUSED(receiver);
    if (selector != ST_UnknownSymbol) {
        const char* messageName = ST_Symbol_toString(image, selector);
        printf("For message \'%s\': ", messageName);
    }
    puts(error);
    exit(EXIT_FAILURE);
}


struct ST_Internal_IntrusiveBST_Node {
    struct ST_Internal_IntrusiveBST_Node* left;
    struct ST_Internal_IntrusiveBST_Node* right;
};


void ST_Internal_IntrusiveBST_Node_initialize(struct ST_Internal_IntrusiveBST_Node* node)
{
    node->left = NULL;
    node->right = NULL;
}


static bool ST_Internal_IntrusiveBST_insert(struct ST_Internal_IntrusiveBST_Node** root,
                                            struct ST_Internal_IntrusiveBST_Node* node,
                                            enum ST_Cmp (*comparator)(void*, void*))
{
    struct ST_Internal_IntrusiveBST_Node* current = *root;
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


static struct ST_Internal_IntrusiveBST_Node*
ST_Internal_IntrusiveBST_find(struct ST_Internal_IntrusiveBST_Node** root,
                              void* key,
                              enum ST_Cmp (*comparator)(void*, void*))
{
    struct ST_Internal_IntrusiveBST_Node* current = *root;
    if (!*root) return NULL;
    while (true) {
        switch (comparator(current, key)) {
        case ST_Cmp_Eq:  /* (found) */ return current;
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


struct ST_Internal_MethodMap_Entry {
    struct ST_Internal_IntrusiveBST_Node nodeHeader;
    ST_Symbol selector;
    ST_Method method;
};


static enum ST_Cmp ST_Internal_MethodMap_insertComparator(void* left,
                                                          void* right)
{
    struct ST_Internal_MethodMap_Entry* leftEntry = left;
    struct ST_Internal_MethodMap_Entry* rightEntry = right;
    if (leftEntry->selector > rightEntry->selector) {
        return ST_Cmp_Less;
    } else if (leftEntry->selector < rightEntry->selector) {
        return ST_Cmp_Greater;
    } else {
        return ST_Cmp_Eq;
    }
}


static enum ST_Cmp ST_Internal_MethodMap_findComparator(void* left,
                                                        void* right)
{
    struct ST_Internal_MethodMap_Entry* entry = left;
    ST_Symbol* selector = right;
    if (*selector < entry->selector) {
        return ST_Cmp_Less;
    } else if (*selector > entry->selector) {
        return ST_Cmp_Greater;
    } else {
        return ST_Cmp_Eq;
    }
}


struct ST_Internal_Object {
    struct ST_Internal_MethodMap_Entry* methodTree;
    struct ST_Internal_Object* class;
    struct ST_Internal_Object* super;
    struct ST_Internal_Object** instanceVariables;
    ST_Size instanceVariableCount;
};


void ST_Object_setMethod(ST_Image image,
                         ST_Object object,
                         ST_Symbol selector,
                         ST_Method method)
{
    struct ST_Internal_MethodMap_Entry* entry =
        malloc(sizeof(struct ST_Internal_MethodMap_Entry));
    if (!entry) {
        ST_Internal_raiseError(image, object, ST_UnknownSymbol, "bad malloc");
    }
    ST_Internal_IntrusiveBST_Node_initialize(&entry->nodeHeader);
    entry->selector = selector;
    entry->method = method;
    if (!ST_Internal_IntrusiveBST_insert((struct ST_Internal_IntrusiveBST_Node**)
                                         &((struct ST_Internal_Object*)object)->methodTree,
                                         &entry->nodeHeader,
                                         ST_Internal_MethodMap_insertComparator)) {
        free(entry);
        return;
    }
}


ST_Object ST_Object_sendMessage(ST_Image image,
                                ST_Object receiver,
                                ST_Symbol selector,
                                ST_Size argc,
                                ST_Object argv[])
{
    struct ST_Internal_Object* currentClass = ((struct ST_Internal_Object*)receiver)->class;
    while (true) {
        struct ST_Internal_IntrusiveBST_Node* entry =
            ST_Internal_IntrusiveBST_find((struct ST_Internal_IntrusiveBST_Node**)
                                          &currentClass->methodTree,
                                          &selector,
                                          ST_Internal_MethodMap_findComparator);
        if (entry) {
            return ((struct ST_Internal_MethodMap_Entry*)entry)->method(image, receiver,
                                                                        argc, argv);
        } else {
            /* Note: the current dummy implementation of metaclasses works by having a class
             hold a circular reference to itself, so we need to test self/super-equality
             before walking up the meta-class hierarchy. */
            if (currentClass->super != currentClass) {
                currentClass = currentClass->super;
            } else {
                ST_Internal_raiseError(image, receiver, selector, "method lookup failed");
            }
        }
    }
}


ST_Object ST_Object_getSuper(ST_Image image, ST_Object object)
{
    ST_UNUSED(image);
    return ((struct ST_Internal_Object*)object)->super;
}


ST_Object ST_Object_getClass(ST_Image image, ST_Object object)
{
    ST_UNUSED(image);
    return ((struct ST_Internal_Object*)object)->class;
}


ST_Object ST_Object_getInstanceVar(ST_Image image,
                                   ST_Object object,
                                   ST_Size position)
{
    struct ST_Internal_Object* obj = object;
    if (position >= obj->instanceVariableCount) {
        ST_Internal_raiseError(image, NULL, ST_UnknownSymbol,
                               "Instance variable index out of bounds");
    }
    return obj->instanceVariables[position];
}


void ST_Object_setInstanceVar(ST_Image image,
                              ST_Object object,
                              ST_Size position,
                              ST_Object value)
{
    struct ST_Internal_Object* obj = object;
    if (position >= obj->instanceVariableCount) {
        ST_Internal_raiseError(image, NULL, ST_UnknownSymbol,
                               "Instance variable index out of bounds");
    }
    obj->instanceVariables[position] = value;
}


struct ST_Internal_StringMap_Entry {
    struct ST_Internal_IntrusiveBST_Node nodeHeader;
    char* key;
    void* value;
};


enum ST_Cmp ST_Internal_StringMap_findComparator(void* left, void* right)
{
    struct ST_Internal_StringMap_Entry* entry = left;
    const char* searchKey = right;
    return ST_Internal_clamp(strcmp(entry->key, searchKey),
                             ST_Cmp_Less, ST_Cmp_Greater);
}


enum ST_Cmp ST_Internal_StringMap_insertComparator(void* left, void* right)
{
    struct ST_Internal_StringMap_Entry* lhsEntry = left;
    struct ST_Internal_StringMap_Entry* rhsEntry = right;
    return ST_Internal_clamp(strcmp(lhsEntry->key, rhsEntry->key),
                             ST_Cmp_Less, ST_Cmp_Greater);
}


struct ST_Internal_Image {
    struct ST_Internal_StringMap_Entry* symbolMap;
    ST_Symbol maxSymbol;
    struct ST_Internal_StringMap_Entry* globalScope;
    struct ST_Internal_Object* nilValue;
    struct ST_Internal_Object* trueValue;
    struct ST_Internal_Object* falseValue;
};



ST_Object ST_Image_getGlobal(ST_Image image, const char* varName)
{
    struct ST_Internal_IntrusiveBST_Node* found =
        ST_Internal_IntrusiveBST_find((struct ST_Internal_IntrusiveBST_Node**)
                                      &((struct ST_Internal_Image*)image)->globalScope,
                                      (void*)varName,
                                      ST_Internal_StringMap_findComparator);
    if (!found) {
        ST_Internal_raiseError(image, NULL, ST_UnknownSymbol, "global var lookup failed");
    }
    return ((struct ST_Internal_StringMap_Entry*)found)->value;
}


void ST_Image_setGlobal(ST_Image image, const char* varName, ST_Object value)
{
    struct ST_Internal_Image* extImg = image;
    struct ST_Internal_StringMap_Entry* entry =
        malloc(sizeof(struct ST_Internal_StringMap_Entry));
    if (!entry) {
        ST_Internal_raiseError(image, NULL, ST_UnknownSymbol, "bad malloc");
    }
    ST_Internal_IntrusiveBST_Node_initialize(&entry->nodeHeader);
    entry->key = ST_Internal_strdup(varName);
    entry->value = value;
    if (!ST_Internal_IntrusiveBST_insert((struct ST_Internal_IntrusiveBST_Node**)
                                         &extImg->globalScope,
                                         &entry->nodeHeader,
                                         ST_Internal_StringMap_insertComparator)) {
        free(entry->key);
        free(entry);
    }
}


ST_Object ST_Image_getNilValue(ST_Image image)
{
    return ((struct ST_Internal_Image*)image)->nilValue;
}


ST_Object ST_Image_getTrueValue(ST_Image image)
{
    return ((struct ST_Internal_Image*)image)->trueValue;
}


ST_Object ST_Image_getFalseValue(ST_Image image)
{
    return ((struct ST_Internal_Image*)image)->falseValue;
}


ST_Symbol ST_Image_requestSymbol(ST_Image image,
                                 const char* symbolName)
{
    struct ST_Internal_Image* extCtx = image;
    struct ST_Internal_IntrusiveBST_Node* found =
        ST_Internal_IntrusiveBST_find((struct ST_Internal_IntrusiveBST_Node**)
                                      &extCtx->symbolMap,
                                      (void*)symbolName,
                                      ST_Internal_StringMap_findComparator);
    struct ST_Internal_StringMap_Entry* newEntry = NULL;
    if (found) {
        return (ST_Symbol)((struct ST_Internal_StringMap_Entry*)found)->value;
    }
    newEntry = malloc(sizeof(struct ST_Internal_StringMap_Entry));
    if (!newEntry) {
        ST_Internal_raiseError(image, NULL, ST_UnknownSymbol, "bad malloc");
    }
    ST_Internal_IntrusiveBST_Node_initialize(&newEntry->nodeHeader);
    newEntry->key = ST_Internal_strdup(symbolName);
    if (!newEntry->key) {
        ST_Internal_raiseError(image, NULL, ST_UnknownSymbol, "bad malloc");
    }
    /* Note: void* is big enough to hide an integer symbol id */
    newEntry->value = (void*)(++extCtx->maxSymbol);
    if (!ST_Internal_IntrusiveBST_insert((struct ST_Internal_IntrusiveBST_Node**)
                                         &extCtx->symbolMap,
                                         &newEntry->nodeHeader,
                                         ST_Internal_StringMap_insertComparator)) {
        free(newEntry->key);
        free(newEntry);
        --extCtx->maxSymbol;
        return 0; /* TODO: unwind stack */
    }
    return extCtx->maxSymbol;
}


const char* ST_Internal_recDecodeSymbol(struct ST_Internal_StringMap_Entry* tree,
                                        ST_Symbol symbol)
{
    if ((ST_Symbol)tree->value == symbol) return tree->key;
    if (tree->nodeHeader.left) {
        const char* found =
            ST_Internal_recDecodeSymbol(((struct ST_Internal_StringMap_Entry*)
                                         tree->nodeHeader.left), symbol);
        if (found) return found;
    }
    if (tree->nodeHeader.right) {
        const char* found =
            ST_Internal_recDecodeSymbol(((struct ST_Internal_StringMap_Entry*)
                                         tree->nodeHeader.right), symbol);
        if (found) return found;
    }
    return NULL;
}


const char* ST_Symbol_toString(ST_Image image, ST_Symbol symbol)
{
    struct ST_Internal_Image* img = image;
    struct ST_Internal_StringMap_Entry* symbolMap = img->symbolMap;
    if (symbolMap) {
        return ST_Internal_recDecodeSymbol(symbolMap, symbol);
    }
    return NULL;
}


/*//////////////////////////////////////////////////////////*/
/*/ Object model below here, TODO move to a separate file. /*/
/*//////////////////////////////////////////////////////////*/

static ST_Object ST_Internal_allocateObject(ST_Image image,
                                            ST_Object self,
                                            ST_Size argc,
                                            ST_Object argv[])
{
    ST_UNUSED(argc);
    ST_UNUSED(argv);
    struct ST_Internal_Object* obj = malloc(sizeof(struct ST_Internal_Object));
    if (!obj) {
        ST_Internal_raiseError(image, self, 1, "bad malloc");
    }
    obj->methodTree = NULL;
    /* Note: because the receiver of a 'new' message will always be a class (or metaclass) */
    /* But, still, TODO: implement real metaclasses. */
    obj->class = ((struct ST_Internal_Object*)self)->class;
    obj->super = obj->class->super;
    return obj;
}


static ST_Object ST_Internal_subClass(ST_Image image,
                                      ST_Object self,
                                      ST_Size argc,
                                      ST_Object argv[])
{
    ST_UNUSED(argc);
    ST_UNUSED(argv);
    struct ST_Internal_Object* sub = ST_Internal_allocateObject(image, self, 0, NULL);
    /* TODO: metaclasses */
    sub->class = sub;
    sub->super = ((struct ST_Internal_Object*)self)->class;
    ST_Object_setMethod(image, sub, 1, ST_Internal_allocateObject); /* FIXME: make symbol explicit */
    return sub;
}


/*
TODO: How to handle instance vars!?

Idea: A class object stores the instance variable names, and the
instances of the class store the values in the same array positions.

Issue: We have to support inherited instance variables...

Idea: The instance variable list is a vector, and subclasses store the
inherited vars at the beginning of the vector, and push their own vars
onto the end. (Doesn't really even need to be a vector, could be an array)
*/
static ST_Object ST_Internal_defineInstanceVariables(ST_Image image,
                                                     ST_Object self,
                                                     ST_Size argc,
                                                     ST_Object argv[])
{
    ST_UNUSED(image);
    ST_UNUSED(self);
    ST_UNUSED(argc);
    ST_UNUSED(argv);
    /* struct ST_Internal_Object* class = ((struct ST_Internal_Object*)self)->class; */
    /* Incomplete */
    return ST_Image_getNilValue(image);
}


static void ST_Internal_Image_initObjectModel(struct ST_Internal_Image* image)
{
    struct ST_Internal_Object* object = malloc(sizeof(struct ST_Internal_Object));
    const ST_Symbol new      = ST_Image_requestSymbol(image, "new");
    const ST_Symbol subclass = ST_Image_requestSymbol(image, "subclass");
    const ST_Symbol instVars = ST_Image_requestSymbol(image, "instanceVariableNames");
    if (!object) {
        ST_Internal_raiseError(image, NULL, ST_UnknownSymbol, "bad malloc");
    }
    object->methodTree = NULL;
    object->class = object;
    object->super = object; /* FIXME: should Object super be Object? */
    ST_Object_setMethod(image, object, new,      ST_Internal_allocateObject);
    ST_Object_setMethod(image, object, subclass, ST_Internal_subClass);
    ST_Object_setMethod(image, object, instVars, ST_Internal_defineInstanceVariables);
    ST_Image_setGlobal(image, "Object", object);
    /* FIXME: right now, true, false, and nil are instances of object, I need to check
       what smalltalk dialects typically use for the base classes of these vars. */
    image->nilValue   = ST_Object_sendMessage(image, object, new, 0, NULL);
    image->trueValue  = ST_Object_sendMessage(image, object, new, 0, NULL);
    image->falseValue = ST_Object_sendMessage(image, object, new, 0, NULL);
}


ST_Image ST_Image_create()
{
    struct ST_Internal_Image* image = malloc(sizeof(struct ST_Internal_Image));
    if (!image) {
        ST_Internal_raiseError(image, NULL, ST_UnknownSymbol, "bad malloc");
    }
    image->maxSymbol = ST_UnknownSymbol;
    image->symbolMap = NULL;
    image->globalScope = NULL;
    ST_Internal_Image_initObjectModel(image);
    return image;
}
