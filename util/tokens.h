#ifndef TOKENS_H
#define TOKENS_H

typedef enum ST_Token {
    ST_TOK_LPAREN = 1,
    ST_TOK_RPAREN,
    ST_TOK_BAR,
    ST_TOK_ASSIGN,
    ST_TOK_PERIOD,
    ST_TOK_SEMICOLON,
    ST_TOK_SELECTOR,
    ST_TOK_SELF,
    ST_TOK_SUPER,
    ST_TOK_NIL,
    ST_TOK_TRUE,
    ST_TOK_FALSE,
    ST_TOK_IDENT,
    ST_TOK_INTEGER,
    ST_TOK_COUNT
} ST_Token;

#endif /* TOKENS_H */
