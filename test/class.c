#include "../src/smalltalk.h"
#include <stdlib.h>
#include <string.h>

int testClass(ST_Object context) {
    ST_Object objSymb = ST_symb(context, "Object");
    ST_Object subcSymb = ST_symb(context, "subclass:");
    ST_Object classSymb = ST_symb(context, "class");
    ST_Object newSymb = ST_symb(context, "new");
    ST_Object widjetName = ST_symb(context, "Widjet");

    ST_Object objClass = ST_getGlobal(context, objSymb);
    ST_Object widjetClass =
        ST_sendMsg(context, objClass, subcSymb, 1, &widjetName);
    ST_Object widjetInst = ST_sendMsg(context, widjetClass, newSymb, 0, NULL);

    ST_Object widjetSuper = ST_getSuper(context, widjetInst);
    ST_Object widjetsClass =
        ST_sendMsg(context, widjetInst, classSymb, 0, NULL);

    if (widjetSuper != objClass) {
        return EXIT_FAILURE;
    }
    if (widjetsClass != widjetClass) {
        return EXIT_FAILURE;
    }

    ST_destroyContext(context);

    return EXIT_SUCCESS;
}

int main() {
    ST_Configuration config = ST_DEFAULT_CONFIG;
    ST_Object context = ST_createContext(&config);
    return testClass(context);
}
