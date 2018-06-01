#include "../src/smalltalk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    ST_Context_Configuration config = {{malloc, free, memcpy, memset, 1024}};
    ST_Context context = ST_createContext(&config);

    ST_Object objSymb = ST_requestSymbol(context, "Object");
    ST_Object arrnewSymb = ST_requestSymbol(context, "new:");
    ST_Object newSymb = ST_requestSymbol(context, "new");
    ST_Object rsetSymb = ST_requestSymbol(context, "rawSet:");

    ST_Object cObject = ST_getGlobal(context, objSymb);
    ST_Object cInt =
        ST_getGlobal(context, ST_requestSymbol(context, "Integer"));
    ST_Object cArr = ST_getGlobal(context, ST_requestSymbol(context, "Array"));

    ST_Object ivarNames, cvarNames;

    ST_Object subc;
    ST_Object subcInst;

    ST_Object integer = ST_sendMessage(context, cInt, newSymb, 0, NULL);
    ST_Object argv[3];
    argv[0] = (ST_Object)1;
    ST_sendMessage(context, integer, rsetSymb, 1, argv);

    argv[0] = integer;
    ivarNames = ST_sendMessage(context, cArr, arrnewSymb, 1, argv);
    cvarNames = ST_sendMessage(context, cArr, arrnewSymb, 1, argv);

    argv[0] = ST_getNilValue(context);
    argv[1] = ivarNames;
    argv[2] = cvarNames;
    subc = ST_sendMessage(
        context, cObject,
        ST_requestSymbol(context,
                         "subclass:instanceVariableNames:classVariableNames:"),
        3, argv);

    subcInst = ST_sendMessage(context, subc, newSymb, 0, NULL);

    ST_setIVar(context, subcInst, 0, ST_getTrueValue(context));

    if (ST_getIVarCount(context, subcInst) != 1) {
        puts("failed to allocate slot for instance variable");
        return EXIT_FAILURE;
    }

    if (ST_getIVar(context, subcInst, 0) != ST_getTrueValue(context)) {
        puts("failed to set instance variable");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
