#pragma once
#include <string_view>

namespace rill {

enum class Tok {
    Eof,
    Newline,
    Indent,
    Dedent,
    Ident,
    IntLit,
    StrLit,
    BoolLit,
#define OP(name, text) name,
#include "lex/operators.def"
#undef OP
#define KW(name, text) Kw##name,
#include "lex/keywords.def"
#undef KW
};

struct Token {
    Tok kind;

    // points into the Source buffer, no copies
    std::string_view text;  
    int line, col;          
};

const char* tok_name(Tok t);

}  
// rill namespace.