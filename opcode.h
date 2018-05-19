#ifndef OPCODE_H
#define OPCODE_H

/* Always add to the end, and don't reorder! */
typedef enum ST_VM_Opcode {
    ST_VM_OP_GETGLOBAL,
    ST_VM_OP_SETGLOBAL,
    ST_VM_OP_PUSHNIL,
    ST_VM_OP_PUSHTRUE,
    ST_VM_OP_PUSHFALSE,
    ST_VM_OP_PUSHSYMBOL,
    ST_VM_OP_SENDMESSAGE,
    ST_VM_OP_SETMETHOD,
    ST_VM_OP_RETURN,
    ST_VM_OP_GETIVAR,
    ST_VM_OP_SETIVAR,
} ST_VM_Opcode;

#endif // OPCODE_H
