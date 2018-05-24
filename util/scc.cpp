#include <iostream>
#include <fstream>
#include <array>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

#include "opcode.h"
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

    void pushTrue() { m_bytecode.push_back(ST_VM_OP_PUSHTRUE); }
    void pushFalse() { m_bytecode.push_back(ST_VM_OP_PUSHFALSE); }
    void pushNil() { m_bytecode.push_back(ST_VM_OP_PUSHNIL); }

    void write(const std::string& path) {
        std::fstream out(path, std::ios::binary | std::ios::out);
        for (auto& symbol : m_symbolTable) {
            out << symbol << '\n';
        }
        out << '\n';
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

class AST {
public:
    virtual void codeGen(BytecodeBuilder& builder) = 0;
    virtual ~AST() {}
};

using ASTRef = std::unique_ptr<AST>;

class Expression : public AST {
public:
    // TODO...

    void codeGen(BytecodeBuilder& builder) override {
        throw std::runtime_error("unimplemented method");
    }
};

class True : public AST {
public:
    void codeGen(BytecodeBuilder& builder) override { builder.pushTrue(); }
};

class False : public AST {
public:
    void codeGen(BytecodeBuilder& builder) override { builder.pushFalse(); }
};

class Nil : public AST {
public:
    void codeGen(BytecodeBuilder& builder) override { builder.pushNil(); }
};

class Assignment : public AST {
public:
    Assignment(const std::string& varName, ASTRef value) :
        m_varName(varName), m_value(std::move(value))
    {}

    void codeGen(BytecodeBuilder& builder) override {
        m_value->codeGen(builder);
        builder.setGlobal(m_varName);
    }

private:
    std::string m_varName;
    ASTRef m_value;
};

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: scc [file]" << std::endl;
        return EXIT_FAILURE;
    }

    { // TEST
        Lexer lexer(argv[1]);
        while (auto tok = lexer.lex()) {
            std::cout << tok.text << std::endl;
        }
        BytecodeBuilder builder;
        builder.getGlobal("Object");
        builder.setGlobal("ObjectClone");
        for (int i = 0; i < 100; ++i) {
            builder.pushTrue();
            builder.pushFalse();
            builder.pushNil();
        }
        builder.write("test2.stbc");
    }
}
