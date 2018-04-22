#include "smalltalk.h"

#include <assert.h>
#include <stdio.h>


ST_Object blahMethod(ST_Image image, ST_Object self, ST_Size argc, ST_Object argv[])
{
    (void)image;
    (void)self;
    (void)argc;
    (void)argv;
    puts("blah");
    return ST_Image_getNilValue(image);
}


int main()
{
    ST_Image* image = ST_Image_create();

    const ST_Symbol new      = ST_Image_requestSymbol(image, "new");
    const ST_Symbol subclass = ST_Image_requestSymbol(image, "subclass");
    const ST_Symbol blah     = ST_Image_requestSymbol(image, "blah");

    ST_Object objClass = ST_Image_getGlobal(image, "Object");
    ST_Object animal = ST_Object_sendMessage(image, objClass, subclass, 0, NULL);
    ST_Object animalInst = ST_Object_sendMessage(image, animal, new, 0, NULL);

    ST_Object_setMethod(image, objClass, blah, blahMethod);

    ST_Object_sendMessage(image, animalInst, blah, 0, 0);

    assert(ST_Object_getSuper(image, animal) == objClass);
    assert(ST_Object_getSuper(image, animalInst) == objClass);
}
