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
    {
        size_t i;
        ST_CodeBlock code;
        ST_Object symbolTable[] = {
            ST_Context_requestSymbol(context, "Object"),
            ST_Context_requestSymbol(context, "new"),
            ST_Context_requestSymbol(context, "subclass"),
            ST_Context_requestSymbol(context, "Widjet")};
        ST_Byte instructions[] = {ST_VM_OP_PUSHNIL,
                                  ST_VM_OP_PUSHTRUE,
                                  ST_VM_OP_PUSHFALSE,
                                  ST_VM_OP_PUSHNIL,

                                  ST_VM_OP_GETGLOBAL,
                                  0,
                                  0,

                                  ST_VM_OP_SENDMESSAGE,
                                  1,
                                  0,

                                  ST_VM_OP_GETGLOBAL,
                                  0,
                                  0,

                                  ST_VM_OP_SENDMESSAGE,
                                  2,
                                  0,

                                  ST_VM_OP_SETGLOBAL,
                                  3,
                                  0};
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
        ST_VM_execute(context, &code);
        ST_VM_store(context, "code.st.bc", &code);
    }

    return EXIT_SUCCESS;
}
