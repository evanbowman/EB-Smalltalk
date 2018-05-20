#include <fstream>
#include <array>
#include <iterator>

#include "opcode.h"

// SmallTalk compiler until we can self-host
// Compiler doesn't do anything yet just writes stuff for testing the vm
// Also non-portable due to endianness.

int main() {
    const char* symbols[] = {
        "Object",
        "subclass"
    };
    const uint8_t program[] = {
        ST_VM_OP_GETGLOBAL, 0, 0,
        ST_VM_OP_SENDMESSAGE, 1, 0,
    };
    std::fstream test("test.stbc", std::ios::binary | std::ios::out);
    for (auto symb = std::begin(symbols); symb != std::end(symbols); ++ symb) {
        test << *symb << '\n';
    }
    test << '\n';
    test.write(reinterpret_cast<const char*>(program), sizeof(program));
}
