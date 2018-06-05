#include "../src/smalltalk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int testMembers(ST_Context context) {
    ST_Object objSymb = ST_symb(context, "Object");
    ST_Object arrnewSymb = ST_symb(context, "new:");
    ST_Object newSymb = ST_symb(context, "new");
    ST_Object rsetSymb = ST_symb(context, "rawSet:");

    ST_Object cObject = ST_getGlobal(context, objSymb);
    ST_Object cInt = ST_getGlobal(context, ST_symb(context, "Integer"));
    ST_Object cArr = ST_getGlobal(context, ST_symb(context, "Array"));

    ST_Object ivarNames, cvarNames;

    ST_Object subc;
    ST_Object subcInst;

    ST_Object integer = ST_sendMsg(context, cInt, newSymb, 0, NULL);
    ST_Object argv[3];
    argv[0] = (ST_Object)1;
    ST_sendMsg(context, integer, rsetSymb, 1, argv);

    argv[0] = integer;
    ivarNames = ST_sendMsg(context, cArr, arrnewSymb, 1, argv);
    cvarNames = ST_sendMsg(context, cArr, arrnewSymb, 1, argv);

    argv[0] = ST_getNil(context);
    argv[1] = ivarNames;
    argv[2] = cvarNames;
    subc = ST_sendMsg(
        context, cObject,
        ST_symb(context, "subclass:instanceVariableNames:classVariableNames:"),
        3, argv);

    subcInst = ST_sendMsg(context, subc, newSymb, 0, NULL);

    /* ST_setIVar(context, subcInst, 0, ST_getTrue(context)); */

    /* if (ST_getIVarCount(context, subcInst) != 1) { */
    /*     puts("failed to allocate slot for instance variable"); */
    /*     return EXIT_FAILURE; */
    /* } */

    /* if (ST_getIVar(context, subcInst, 0) != ST_getTrue(context)) { */
    /*     puts("failed to set instance variable"); */
    /*     return EXIT_FAILURE; */
    /* } */
    return EXIT_SUCCESS;
}

int main() {
    ST_Context_Configuration config = {{malloc, free, memcpy, memset, 1024}};
    ST_Context context = ST_createContext(&config);
    ST_GC_pause(context);
    return testMembers(context);
}
