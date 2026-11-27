#pragma once
#include <vector>
#include "common/diag.hpp"
#include "common/source.hpp"
#include "lex/token.hpp"

namespace rill {

class Lexer {
public:
    Lexer(const Source& src, Diagnostics& diags) : src_(src), diags_(diags) {}
    std::vector<Token> run();

private:
    char peek(size_t ahead = 0) const;
    char advance();
    Token make(Tok kind, size_t start, int line, int col) const;
    Token number();
    Token word();
    bool operator_at(Token& out);

    const Source& src_;
    Diagnostics& diags_;
    size_t pos_ = 0;
    int line_ = 1, col_ = 1;
};

}  // rill namespace.