#include "smalltalk.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

ST_Object blahMethod(ST_Context context, ST_Object self, ST_Object argv[]) {
    (void)context;
    (void)self;
    (void)argv;
    puts("blah");
    return ST_Context_getNilValue(context);
}

#include "opcode.h"

int main() {
    ST_Context *context = ST_Context_create();

    ST_SUBCLASS(context, "Object", "Test");

    ST_SETMETHOD(context, "Test", "blah", blahMethod, 0);

    {
        size_t i;
        ST_Code code;
        ST_Object symbolTable[] = {
            ST_Context_requestSymbol(context, "Object"),
            ST_Context_requestSymbol(context, "new"),
            ST_Context_requestSymbol(context, "subclass"),
            ST_Context_requestSymbol(context, "Widjet"),
            ST_Context_requestSymbol(context, "blah"),
            ST_Context_requestSymbol(context, "Test")};
        ST_Byte instructions[] = {
            ST_VM_OP_GETGLOBAL,
            0,
            0,

            ST_VM_OP_SENDMESSAGE,
            2,
            0, /* subclass */

            ST_VM_OP_DUP, /* SETMETHOD consumes class */

            ST_VM_OP_SETMETHOD,
            4,
            0,
            /* blah */ 0, /* no arg */
            7,
            0,
            0,
            0, /* Method len */

            ST_VM_OP_GETGLOBAL,
            5,
            0,

            ST_VM_OP_SENDMESSAGE,
            4,
            0,

            ST_VM_OP_RETURN,

            ST_VM_OP_SENDMESSAGE, /* Send new to the class */
            1,
            0,

            ST_VM_OP_SENDMESSAGE, /* call blah */
            4,
            0,

            ST_VM_OP_POP, /* pop nil result */
        };
        for (i = 0; i < sizeof(instructions); ++i) {
            printf("%02x ", instructions[i]);
            if ((i + 1) % 16 == 0) {
                puts("");
            }
        }
        puts("");
        code.instructions = instructions;
        code.length = sizeof(instructions);
        code.symbTab = symbolTable;
        code.symbTabSize = sizeof(symbolTable) / sizeof(ST_Object);
        ST_VM_execute(context, &code, 0);
        ST_VM_store(context, "test.stbc", &code);
        /* ST_Code codecpy = ST_VM_load(context, "boolean.stbc"); */
        /* ST_VM_store(context, "boolean.stbc", &codecpy); */
    }

    return EXIT_SUCCESS;
}
