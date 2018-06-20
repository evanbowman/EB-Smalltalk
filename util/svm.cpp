#include <iostream>
#include <fstream>
#include <sstream>
#include <stdlib.h>
#include <string.h>
#include "../src/smalltalk.h"

/* Standalone version of the vm & runtime */

int main(int argc, char **argv) {
    if (argc != 2) {
        puts("usage: svm [file]");
        return EXIT_FAILURE;
    }
    ST_Configuration config = ST_DEFAULT_CONFIG;
    ST_Object context = ST_createContext(&config);
    std::ifstream input(argv[1], std::ios::binary);
    std::stringstream buffer;
    buffer << input.rdbuf();
    std::string programBytes = buffer.str();
    ST_Code program = ST_VM_load(context,
                                 (const ST_U8 *)programBytes.c_str(),
                                 programBytes.size());
    ST_VM_execute(context, &program, 0);
    return EXIT_SUCCESS;
}
