#include "../src/opcode.h"

#include <fstream>
#include <iostream>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include "../src/smalltalk.h"
#include <string.h>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: stbc-printer [file]" << std::endl;
        return EXIT_FAILURE;
    }
    ST_Context_Configuration config = {{malloc, free, memcpy, memset, 1024}};
    ST_Context context = ST_createContext(&config);
    std::ifstream input(argv[1], std::ios::binary);
    std::stringstream buffer;
    buffer << input.rdbuf();
    std::string programBytes = buffer.str();
    ST_Code program = ST_VM_load(context,
                                 (const ST_U8 *)programBytes.c_str(),
                                 programBytes.size());
    for (size_t i = 0; i < program.length; ++i) {
        switch (program.instructions[i]) {
        case ST_VM_OP_GETGLOBAL: {
            std::cout << std::setw(14) << std::left << "GETGLOBAL";
            uint16_t symbolIndex = (uint16_t)program.instructions[i + 1] |
                                  ((uint16_t)program.instructions[i + 2] << 8);
            std::cout << ST_Symbol_toString(context, program.symbTab[symbolIndex]) << std::endl;
            i += 2;
        } break;

        case ST_VM_OP_SETGLOBAL: {
            std::cout << std::setw(14) << std::left << "SETGLOBAL";
            uint16_t symbolIndex = (uint16_t)program.instructions[i + 1] |
                ((uint16_t)program.instructions[i + 2] << 8);
            std::cout << ST_Symbol_toString(context, program.symbTab[symbolIndex]) << std::endl;
            i += 2;
        } break;

        case ST_VM_OP_PUSHNIL:
            std::cout << "PUSHNIL" << std::endl;
            break;

        case ST_VM_OP_PUSHTRUE:
            std::cout << "PUSHTRUE" << std::endl;
            break;

        case ST_VM_OP_PUSHFALSE:
            std::cout << "PUSHFALSE" << std::endl;
            break;

        case ST_VM_OP_PUSHSYMBOL: {
            std::cout << std::setw(14) << std::left << "PUSHSYMBOL";
            uint16_t symbolIndex = (uint16_t)program.instructions[i + 1] |
                            ((uint16_t)program.instructions[i + 2] << 8);
            std::cout << ST_Symbol_toString(context, program.symbTab[symbolIndex]) << std::endl;
            i += 2;
        } break;

        case ST_VM_OP_SENDMSG: {
            std::cout << std::setw(14) << std::left << "SENDMSG";
            uint16_t symbolIndex = (uint16_t)program.instructions[i + 1] |
                ((uint16_t)program.instructions[i + 2] << 8);
            std::cout << ST_Symbol_toString(context, program.symbTab[symbolIndex]) << std::endl;
            i += 2;
        } break;

        case ST_VM_OP_DUP:
            std::cout << "DUP" << std::endl;
            break;

        default:
            std::cerr << "printer encountered unknown bytecode" << std::endl;
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}
