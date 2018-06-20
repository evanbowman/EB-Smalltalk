#include "../src/smalltalk.h"
#include <stdlib.h>
#include <string.h>

int main() {
    ST_Configuration config = ST_DEFAULT_CONFIG;
    ST_Object context = ST_createContext(&config);
    const char *testSymbStr = "TEST";
    ST_Object testSymb = ST_symb(context, testSymbStr);
    if (strcmp(ST_Symbol_toString(context, testSymb), testSymbStr) != 0) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
