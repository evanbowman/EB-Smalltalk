#include <iostream>
#include <fstream>

#include "antlr4-runtime.h"

#include "SmalltalkLexer.h"
#include "SmalltalkParser.h"
#include "SmalltalkBaseListener.h"

int main(int argc, char** argv) {
    std::ifstream stream(argv[1]);
    antlr4::ANTLRInputStream input(stream);
    SmalltalkLexer lexer(&input);
    antlr4::CommonTokenStream tokens(&lexer);
    SmalltalkParser parser(&tokens);

    antlr4::tree::ParseTree *tree = parser.script();

    std::cout << tree->toStringTree() << std::endl;

    // antlr4::tree::ParseTreeWalker::DEFAULT.walk(&listener, tree);
}
