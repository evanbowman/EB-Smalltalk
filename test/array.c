#include "../src/smalltalk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    ST_Context_Configuration config = {{malloc, free, memcpy, memset, 1024}};
    ST_Context context = ST_createContext(&config);

    ST_Object arrnewSymb = ST_symb(context, "new:");
    ST_Object newSymb = ST_symb(context, "new");
    ST_Object rsetSymb = ST_symb(context, "rawSet:");
    ST_Object rgetSymb = ST_symb(context, "rawGet");
    ST_Object lengthSymb = ST_symb(context, "length");
    ST_Object arrayAt = ST_symb(context, "at:");
    ST_Object arrayPut = ST_symb(context, "at:put:");

    ST_Object cInt = ST_getGlobal(context, ST_symb(context, "Integer"));
    ST_Object cArr = ST_getGlobal(context, ST_symb(context, "Array"));
    ST_Object integer;
    ST_Object array;

    ST_Object argv[2];
    argv[0] = (ST_Object)10;
    integer = ST_sendMsg(context, cInt, newSymb, 0, NULL);
    ST_sendMsg(context, integer, rsetSymb, 1, argv);

    argv[0] = integer;
    array = ST_sendMsg(context, cArr, arrnewSymb, 1, argv);

    if ((intptr_t)ST_sendMsg(context,
                             ST_sendMsg(context, array, lengthSymb, 0, NULL),
                             rgetSymb, 0, NULL) != 10) {
        puts("array length method returned unexpected value");
        return EXIT_FAILURE;
    }

    if (ST_sendMsg(context, array, arrayAt, 1, argv) != ST_getNil(context)) {
        puts("array out of bounds access did not return nil");
        return EXIT_FAILURE;
    }

    argv[0] = (ST_Object)5;
    ST_sendMsg(context, integer, rsetSymb, 1, argv);

    argv[0] = integer;
    argv[1] = ST_getTrue(context);
    ST_sendMsg(context, array, arrayPut, 2, argv);

    if (ST_sendMsg(context, array, arrayAt, 1, argv) != ST_getTrue(context)) {
        puts("array at:put: failed");
        return EXIT_FAILURE;
    }

    argv[0] = (ST_Object)6;
    ST_sendMsg(context, integer, rsetSymb, 1, argv);
    argv[0] = integer;
    if (ST_sendMsg(context, array, arrayAt, 1, argv) != ST_getNil(context)) {
        puts("uninitialize inbound array slot does not contain nil");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
