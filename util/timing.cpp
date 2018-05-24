extern "C" {
#include "smalltalk.h"
}
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ST_Object blahMethod(ST_Context context, ST_Object self, ST_Object argv[])
{
    (void)self;
    (void)argv;
    puts("blah");
    return ST_getNilValue(context);
}

int main()
{
    ST_Context_Configuration config = {{malloc, free, memcpy, memset}};
    ST_Context context = ST_createContext(&config);

    const ST_Object newSymb  = ST_requestSymbol(context, "new");
    const ST_Object subclass = ST_requestSymbol(context, "subclass");
    const ST_Object blah     = ST_requestSymbol(context, "blah");
    const ST_Object objSymb  = ST_requestSymbol(context, "Object");
    
    ST_Object objClass = ST_getGlobal(context, objSymb);
    ST_Object animal = ST_Object_sendMessage(context, objClass, subclass, 0, NULL);
    ST_Object animalInst = ST_Object_sendMessage(context, animal, newSymb, 0, NULL);

    ST_Object_setMethod(context, objClass, blah, blahMethod, 0);

    ST_Object_sendMessage(context, animalInst, blah, 0, 0);
}
