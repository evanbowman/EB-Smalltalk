typedef enum ST_VM_Opcode {
    /* Noarg */
    ST_VM_OP_PUSHNIL,
    ST_VM_OP_PUSHTRUE,
    ST_VM_OP_PUSHFALSE,
    ST_VM_OP_PUSHSUPER,
    ST_VM_OP_DUP,
    ST_VM_OP_POP,
    ST_VM_OP_SWAP,
    ST_VM_OP_RETURN,

    /* One 16bit arg */
    ST_VM_OP_GETGLOBAL,
    ST_VM_OP_SETGLOBAL,
    ST_VM_OP_GETIVAR,
    ST_VM_OP_SETIVAR,
    ST_VM_OP_SENDMSG,
    ST_VM_OP_PUSHSYMBOL,

    /* Misc */
    ST_VM_OP_SETMETHOD,

    /* End. Don't exceed 255 */
    ST_VM_OP_COUNT = 256
} ST_VM_Opcode;
