#include "../src/smalltalk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    ST_Context_Configuration config = {{malloc, free, memcpy, memset, 1024}};
    ST_Context context = ST_createContext(&config);

    ST_Object arrnewSymb = ST_requestSymbol(context, "new:");
    ST_Object newSymb = ST_requestSymbol(context, "new");
    ST_Object rsetSymb = ST_requestSymbol(context, "rawSet:");
    ST_Object rgetSymb = ST_requestSymbol(context, "rawGet");
    ST_Object lengthSymb = ST_requestSymbol(context, "length");
    ST_Object arrayAt = ST_requestSymbol(context, "at:");
    ST_Object arrayPut = ST_requestSymbol(context, "at:put:");

    ST_Object cInt =
        ST_getGlobal(context, ST_requestSymbol(context, "Integer"));
    ST_Object cArr = ST_getGlobal(context, ST_requestSymbol(context, "Array"));
    ST_Object integer;
    ST_Object array;

    ST_Object argv[2];
    argv[0] = (ST_Object)10;
    integer = ST_sendMessage(context, cInt, newSymb, 0, NULL);
    ST_sendMessage(context, integer, rsetSymb, 1, argv);

    argv[0] = integer;
    array = ST_sendMessage(context, cArr, arrnewSymb, 1, argv);

    if ((intptr_t)ST_sendMessage(
            context, ST_sendMessage(context, array, lengthSymb, 0, NULL),
            rgetSymb, 0, NULL) != 10) {
        puts("array length method returned unexpected value");
        return EXIT_FAILURE;
    }

    if (ST_sendMessage(context, array, arrayAt, 1, argv) !=
        ST_getNilValue(context)) {
        puts("array out of bounds access did not return nil");
        return EXIT_FAILURE;
    }

    argv[0] = (ST_Object)5;
    ST_sendMessage(context, integer, rsetSymb, 1, argv);

    argv[0] = integer;
    argv[1] = ST_getTrueValue(context);
    ST_sendMessage(context, array, arrayPut, 2, argv);

    if (ST_sendMessage(context, array, arrayAt, 1, argv) !=
        ST_getTrueValue(context)) {
        puts("array at:put: failed");
        return EXIT_FAILURE;
    }

    argv[0] = (ST_Object)6;
    ST_sendMessage(context, integer, rsetSymb, 1, argv);
    argv[0] = integer;
    if (ST_sendMessage(context, array, arrayAt, 1, argv) !=
        ST_getNilValue(context)) {
        puts("uninitialize inbound array slot does not contain nil");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
