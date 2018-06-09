%{
#include "tokens.h"
int fileno(FILE *stream);
%}

%option nounput
%option noinput
%option noyywrap

%%
"("             { return ST_TOK_LPAREN;    }
")"             { return ST_TOK_RPAREN;    }
"|"             { return ST_TOK_BAR;       }
":="            { return ST_TOK_ASSIGN;    }
"."             { return ST_TOK_PERIOD;    }
";"             { return ST_TOK_SEMICOLON; }
"self"          { return ST_TOK_SELF;      }
"super"         { return ST_TOK_SUPER;     }
"nil"           { return ST_TOK_NIL;       }
"true"          { return ST_TOK_TRUE;      }
"false"         { return ST_TOK_FALSE;     }
[0-9]+          { return ST_TOK_INTEGER;   }
[A-Za-z0-9]+":" { return ST_TOK_SELECTOR;  }
[A-Za-z0-9]+    { return ST_TOK_IDENT;     }
[ /t/n/r]
%%
