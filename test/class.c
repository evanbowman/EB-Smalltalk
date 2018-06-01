#include "../src/smalltalk.h"
#include <stdlib.h>
#include <string.h>

int main() {
    ST_Context_Configuration config = {{malloc, free, memcpy, memset, 1024}};
    ST_Context context = ST_createContext(&config);
    ST_Object objSymb = ST_requestSymbol(context, "Object");
    ST_Object subcSymb = ST_requestSymbol(context, "subclass");
    ST_Object classSymb = ST_requestSymbol(context, "class");
    ST_Object newSymb = ST_requestSymbol(context, "new");

    ST_Object objClass = ST_getGlobal(context, objSymb);
    ST_Object widjetClass =
        ST_sendMessage(context, objClass, subcSymb, 0, NULL);
    ST_Object widjetInst =
        ST_sendMessage(context, widjetClass, newSymb, 0, NULL);

    ST_Object widjetSuper = ST_getSuper(context, widjetInst);
    ST_Object widjetsClass =
        ST_sendMessage(context, widjetInst, classSymb, 0, NULL);

    if (widjetSuper != objClass) {
        return EXIT_FAILURE;
    }
    if (widjetsClass != widjetClass) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
