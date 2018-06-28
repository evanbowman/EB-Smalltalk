#include "../src/smalltalk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int testNumber(ST_Object ctx) {
    ST_Object integerSymb = ST_symb(ctx, "Integer");
    ST_Object newSymb = ST_symb(ctx, "new");
    ST_Object cNumb = ST_getGlobal(ctx, integerSymb);
    ST_Object cArray = ST_getGlobal(ctx, ST_symb(ctx, "Array"));
    ST_Object arrNewSymb = ST_symb(ctx, "new:");
    ST_Object rawSetSymb = ST_symb(ctx, "rawSet:");
    ST_Object atSymb = ST_symb(ctx, "at:");
    ST_Object putSymb = ST_symb(ctx, "at:put:");

    ST_Object *locals = ST_pushLocals(ctx, 1);

    /* arr := Array new: 10. */
    ST_Object argv[2];
    ST_Object index = ST_sendMsg(ctx, cNumb, newSymb, 0, NULL);
    argv[0] = (void *)10;
    ST_sendMsg(ctx, index, rawSetSymb, 1, argv);
    argv[0] = index;
    locals[0] = ST_sendMsg(ctx, cArray, arrNewSymb, 1, argv);

    /* arr at: 3 put: true. */
    argv[0] = (void *)3;
    ST_sendMsg(ctx, index, rawSetSymb, 1, argv);
    argv[0] = index;
    argv[1] = ST_getTrue(ctx);
    ST_sendMsg(ctx, locals[0], putSymb, 2, argv);

    ST_GC_run(ctx);

    { /* Verify that after gc compaction, array is still valid and
         index 3 contains true. */
        index = ST_sendMsg(ctx, cNumb, newSymb, 0, NULL);
        argv[0] = (void *)3;
        ST_sendMsg(ctx, index, rawSetSymb, 1, argv);
        argv[0] = index;
        if (ST_sendMsg(ctx, locals[0], atSymb, 1, argv) != ST_getTrue(ctx)) {
            puts("GC compaction created error state");
            return EXIT_FAILURE;
        } else {
            puts("ALL CLEAR!");
        }
    }

    ST_popLocals(ctx);

    return EXIT_SUCCESS;
}

int main() {
    ST_Configuration config = ST_DEFAULT_CONFIG;
    ST_Object ctx = ST_createContext(&config);
    return testNumber(ctx);
}
