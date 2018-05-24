#include "opcode.h"

#include <fstream>
#include <iostream>
#include <vector>
#include <algorithm>
#include <iomanip>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: stbc-printer [file]" << std::endl;
        return EXIT_FAILURE;
    }
    std::ifstream input(argv[1], std::ios::binary);
    std::vector<std::string> symbolTable;
    std::string line;
    while (true) {
        if (std::getline(input, line)) {
            if (line == "") {
                break;
            } else {
                symbolTable.push_back(line);
            }
        } else {
            std::cout << "garbled symbol table" << std::endl;
        }
    }
    std::vector<uint8_t> bytecode;
    std::copy(std::istream_iterator<uint8_t>(input),
              std::istream_iterator<uint8_t>(),
              std::back_inserter(bytecode));
    const size_t bytecodeSize = bytecode.size();
    for (size_t i = 0; i < bytecodeSize; ++i) {
        switch (bytecode[i]) {
        case ST_VM_OP_GETGLOBAL: {
            std::cout << std::setw(14) << std::left << "GETGLOBAL";
            uint16_t symbolIndex = (uint16_t)bytecode[i + 1] |
                                  ((uint16_t)bytecode[i + 2] << 8);
            std::cout << symbolTable[symbolIndex] << std::endl;
            i += 2;
        } break;

        case ST_VM_OP_SETGLOBAL: {
            std::cout << std::setw(14) << std::left << "SETGLOBAL";
            uint16_t symbolIndex = (uint16_t)bytecode[i + 1] |
                ((uint16_t)bytecode[i + 2] << 8);
            std::cout << symbolTable[symbolIndex] << std::endl;
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
            uint16_t symbolIndex = (uint16_t)bytecode[i + 1] |
                            ((uint16_t)bytecode[i + 2] << 8);
            std::cout << symbolTable[symbolIndex] << std::endl;
            i += 2;
        } break;

        case ST_VM_OP_SENDMESSAGE: {
            std::cout << std::setw(14) << std::left << "SENDMESSAGE";
            uint16_t symbolIndex = (uint16_t)bytecode[i + 1] |
                ((uint16_t)bytecode[i + 2] << 8);
            std::cout << symbolTable[symbolIndex] << std::endl;
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
