#include "../src/smalltalk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
    ST_Context_Configuration config = {{malloc, free, memcpy, memset, 1024}};
    ST_Context context = ST_createContext(&config);
    ST_Object integerSymb = ST_requestSymbol(context, "Integer");
    ST_Object newSymb = ST_requestSymbol(context, "new");
    ST_Object addSymb = ST_requestSymbol(context, "+");
    ST_Object subSymb = ST_requestSymbol(context, "-");
    ST_Object mulSymb = ST_requestSymbol(context, "*");
    ST_Object divSymb = ST_requestSymbol(context, "/");
    ST_Object rawgetSymb = ST_requestSymbol(context, "rawGet");
    ST_Object rawsetSymb = ST_requestSymbol(context, "rawSet:");

    ST_Object cNumb = ST_getGlobal(context, integerSymb);

    ST_Object num1 = ST_sendMessage(context, cNumb, newSymb, 0, NULL);
    ST_Object num2 = ST_sendMessage(context, cNumb, newSymb, 0, NULL);

    ST_Object v1[] = {(void *)6};
    ST_Object v2[] = {(void *)3};

    ST_Object result;

    ST_Object argv[1];
    argv[0] = num2;

    ST_sendMessage(context, num1, rawsetSymb, 1, v1);
    ST_sendMessage(context, num2, rawsetSymb, 1, v2);

    result = ST_sendMessage(context, num1, addSymb, 1, argv);

    if ((int)(int32_t)(intptr_t)ST_sendMessage(context, result, rawgetSymb, 0,
                                               NULL) != 9) {
        return EXIT_FAILURE;
    }

    result = ST_sendMessage(context, num1, subSymb, 1, argv);

    if ((int)(int32_t)(intptr_t)ST_sendMessage(context, result, rawgetSymb, 0,
                                               NULL) != 3) {
        return EXIT_FAILURE;
    }

    result = ST_sendMessage(context, num1, mulSymb, 1, argv);

    if ((int)(int32_t)(intptr_t)ST_sendMessage(context, result, rawgetSymb, 0,
                                               NULL) != 18) {
        return EXIT_FAILURE;
    }

    result = ST_sendMessage(context, num1, divSymb, 1, argv);

    if ((int)(int32_t)(intptr_t)ST_sendMessage(context, result, rawgetSymb, 0,
                                               NULL) != 2) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
