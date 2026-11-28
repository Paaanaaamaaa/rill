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
    const Token& peek(size_t n) const { return toks_[i_ + n < toks_.size() ? i_ + n : toks_.size() - 1]; }
    const Token& after() const { return peek(1); }
    Token take();
    void expect(Tok kind, const char* msg);
    void end_line();
    [[noreturn]] void fail(const char* msg);
    NodePtr node(NodeKind k, const Token& t);
    NodePtr verb_node(NodeKind k);

    NodePtr statement();
    NodePtr annotated();
    NodePtr type_ref();
    NodePtr definition(NodeKind kind);
    NodePtr clause();
    NodePtr clause_body();
    void patterns(Node& into);
    NodePtr match(NodePtr subject);
    NodePtr orbit();
    NodePtr isomorph();
    NodePtr lattice();
    NodePtr manifold();
    NodePtr expr(bool train_ok = true);
    NodePtr strand();
    NodePtr atom();
    NodePtr list();

    const std::vector<Token>& toks_;
    Diagnostics& diags_;
    size_t i_ = 0;
    // a multi-line match eats the newline that ends its statement,
    // this remembers that so end_line() doesn't complain
    bool ate_newline_ = false;
};

}  // namespace rill
