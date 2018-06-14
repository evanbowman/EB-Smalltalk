#include <iostream>
#include <fstream>
#include <array>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

#include "../src/opcode.h"
#include "tokens.h"

extern "C" {
    void* yy_scan_string(const char*);

    char* yytext;

    int yylex();
}

class Lexer {
public:
    Lexer(const std::string& sourcePath) {
        std::ifstream in(sourcePath);
        std::stringstream buffer;
        buffer << in.rdbuf();
        yy_scan_string(buffer.str().c_str());
    }

    struct Token {
        ST_Token id;
        std::string text;

        operator bool() { return id != 0; }
    };

    Token lex() { return {(ST_Token)yylex(), yytext}; }
};

class BytecodeBuilder {
public:
    void setGlobal(const std::string& varName) {
        m_bytecode.push_back(ST_VM_OP_SETGLOBAL);
        writeLE16(getSymbol(varName));
    }

    void getGlobal(const std::string& varName) {
        m_bytecode.push_back(ST_VM_OP_GETGLOBAL);
        writeLE16(getSymbol(varName));
    }

    void sendMsg(const std::string& selector) {
        m_bytecode.push_back(ST_VM_OP_SENDMSG);
        writeLE16(getSymbol(selector));
    }

    void pushTrue() { m_bytecode.push_back(ST_VM_OP_PUSHTRUE); }
    void pushFalse() { m_bytecode.push_back(ST_VM_OP_PUSHFALSE); }
    void pushNil() { m_bytecode.push_back(ST_VM_OP_PUSHNIL); }

    void write(const std::string& path) {
        std::fstream out(path, std::ios::binary | std::ios::out);
        for (auto& symbol : m_symbolTable) {
            out << symbol << '\0';
        }
        out << '\0';
        out.write(reinterpret_cast<const char*>(m_bytecode.data()),
                  m_bytecode.size());
    }

private:
    uint16_t getSymbol(const std::string& name) {
        const size_t numSymbols = m_symbolTable.size();
        for (size_t i = 0; i < numSymbols; ++i) {
            if (m_symbolTable[i] == name) {
                return i;
            }
        }
        m_symbolTable.push_back(name);
        return numSymbols;
    }

    void writeLE16(uint16_t value) {
        // FIXME: non portable
        m_bytecode.push_back(((uint8_t*)&value)[0]);
        m_bytecode.push_back(((uint8_t*)&value)[1]);
    }

    std::vector<std::string> m_symbolTable;
    std::vector<uint8_t> m_bytecode;
};

void parseExpression(Lexer& lexer) {
    while (auto tok = lexer.lex()) {
        std::cout << tok.text << std::endl;
    }
}

void error(const std::string& err) {
    std::cerr << "error: " << err << std::endl;
    exit(1);
}

void parseLocalVars(Lexer& lexer) {
    Lexer::Token tok;
    while ((tok = lexer.lex()).id != ST_TOK_BAR) {
        if (tok.id != ST_TOK_IDENT) {
            error("expected identifier");
        }
    }
}

void parseLine(Lexer& lexer) {
    const auto tok = lexer.lex();
    if (tok.id == ST_TOK_BAR) {
        parseLocalVars(lexer);
    }
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: scc [file]" << std::endl;
        return EXIT_FAILURE;
    }

    { // TEST
        Lexer lexer(argv[1]);
        parseLine(lexer);
        BytecodeBuilder builder;
        builder.getGlobal("Object");
        builder.setGlobal("ObjectClone");
        builder.getGlobal("Object");
        builder.sendMsg("new");
        for (int i = 0; i < 10; ++i) {
            builder.pushNil();
        }
        builder.write("test2.stbc");
    }
}
