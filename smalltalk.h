#pragma once
#ifndef SMALLTALK_H
#define SMALLTALK_H

typedef unsigned long   ST_Size;
typedef void*           ST_OpaqueStruct;
typedef ST_OpaqueStruct ST_Image;
typedef ST_OpaqueStruct ST_Object;
typedef ST_Size         ST_Symbol;

typedef ST_Object (*ST_Method)(ST_Image,
                               ST_Object,
                               ST_Size,
                               ST_Object[]);

void ST_Object_setMethod(ST_Image image,
                         ST_Object object,
                         ST_Symbol selector,
                         ST_Method method);

ST_Object ST_Object_sendMessage(ST_Image image,
                                ST_Object receiver,
                                ST_Symbol selector,
                                ST_Size argc,
                                ST_Object argv[]);

ST_Object ST_Object_getSuper(ST_Image image,
                             ST_Object object);

ST_Object ST_Object_getClass(ST_Image image,
                             ST_Object object);

ST_Object ST_Object_getInstanceVar(ST_Image image,
                                   ST_Object object,
                                   ST_Size position);

void ST_Object_setInstanceVar(ST_Image image,
                              ST_Object object,
                              ST_Size position,
                              ST_Object value);

ST_Image ST_Image_create();

ST_Object ST_Image_getGlobal(ST_Image image, const char* varName);

ST_Object ST_Image_getNilValue(ST_Image image);
ST_Object ST_Image_getTrueValue(ST_Image image);
ST_Object ST_Image_getFalseValue(ST_Image image);

void ST_Image_setGlobal(ST_Image image, const char* varName, ST_Object value);

ST_Symbol ST_Image_requestSymbol(ST_Image image,
                                 const char* symbolName);

const char* ST_Symbol_toString(ST_Image image,
                               ST_Symbol symbol);

#endif  /* SMALLTALK_H */
