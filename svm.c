#include <assert.h>
#include <stdio.h>

#include "smalltalk.h"

/* Standalone version of the vm & runtime */

int main(int argc, char **argv) {
    if (argc != 2) {
        puts("usage: svm [file]");
        return EXIT_FAILURE;
    }
    ST_Context context = ST_Context_create();
    ST_Code code = ST_VM_load(context, argv[1]);
    ST_VM_execute(context, &code, 0);
    return EXIT_SUCCESS;
}
