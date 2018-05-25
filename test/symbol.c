#include "../src/smalltalk.h"
#include <stdlib.h>
#include <string.h>

int main() {
    ST_Context_Configuration config = {{malloc, free, memcpy, memset}};
    ST_Context context = ST_createContext(&config);
    const char *testSymbStr = "TEST";
    ST_Object testSymb = ST_requestSymbol(context, testSymbStr);
    if (strcmp(ST_Symbol_toString(context, testSymb), testSymbStr) != 0) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
