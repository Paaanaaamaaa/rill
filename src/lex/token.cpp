#include "lex/token.hpp"

namespace rill {

const char* tok_name(Tok t) {
    switch (t) {
        case Tok::Eof:     return "Eof";
        case Tok::Newline: return "Newline";
        case Tok::Indent:  return "Indent";
        case Tok::Dedent:  return "Dedent";
        case Tok::Ident:   return "Ident";
        case Tok::IntLit:  return "IntLit";
#define OP(name, text) case Tok::name: return #name;
#include "lex/operators.def"
#undef OP
#define KW(name, text) case Tok::Kw##name: return "Kw" #name;
#include "lex/keywords.def"
#undef KW
    }
    return "?";
}

}  // rill namespace..