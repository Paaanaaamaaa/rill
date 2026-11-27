#pragma once
#include <vector>
#include "common/diag.hpp"
#include "lex/token.hpp"
#include "parse/ast.hpp"

namespace rill {

class Parser {
public:
    Parser(const std::vector<Token>& toks, Diagnostics& diags) : toks_(toks), diags_(diags) {}
    std::vector<NodePtr> parse();

private:
    struct Abort {};

    const Token& cur() const { return toks_[i_]; }
    const Token& after() const { return toks_[i_ + 1 < toks_.size() ? i_ + 1 : i_]; }
    Token take();
    void expect(Tok kind, const char* msg);
    [[noreturn]] void fail(const char* msg);
    NodePtr node(NodeKind k, const Token& t);
    NodePtr verb_node(NodeKind k);

    NodePtr statement();
    NodePtr expr(bool train_ok = true);
    NodePtr strand();
    NodePtr atom();
    NodePtr list();

    const std::vector<Token>& toks_;
    Diagnostics& diags_;
    size_t i_ = 0;
};

}  // namespace rill
