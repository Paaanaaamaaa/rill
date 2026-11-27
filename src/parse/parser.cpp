#include "parse/parser.hpp"

namespace rill {
namespace {

bool is_verb(Tok k) {
    switch (k) {
        case Tok::Plus: case Tok::Minus: case Tok::Star: case Tok::Slash:
        case Tok::Percent: case Tok::Hash: case Tok::Caret: case Tok::Bang:
        case Tok::Lt: case Tok::Gt: case Tok::Eq: case Tok::Amp:
        case Tok::At: case Tok::Power: case Tok::Shl: case Tok::Shr:
        case Tok::Print: case Tok::Tilde:
            return true;
        default:
            return false;
    }
}

bool starts_atom(Tok k) {
    return k == Tok::IntLit || k == Tok::StrLit || k == Tok::Ident ||
           k == Tok::LParen || k == Tok::LBracket;
}

}  // namespace

Token Parser::take() {
    Token t = toks_[i_];
    if (t.kind != Tok::Eof) ++i_;  // never step past Eof
    return t;
}

void Parser::fail(const char* msg) {
    diags_.error(cur().line, cur().col, static_cast<int>(cur().text.size()), msg);
    throw Abort{};
}

void Parser::expect(Tok kind, const char* msg) {
    if (cur().kind != kind) fail(msg);
    take();
}

NodePtr Parser::node(NodeKind k, const Token& t) {
    auto n = std::make_unique<Node>();
    n->kind = k;
    n->tok = t;
    return n;
}

// takes the verb token, and an adverb if one is glued to it with no space
NodePtr Parser::verb_node(NodeKind k) {
    Token v = take();
    NodePtr n = node(k, v);
    const Token& a = cur();
    bool glued = a.line == v.line && a.col == v.col + static_cast<int>(v.text.size());
    if (glued && (a.kind == Tok::Slash || a.kind == Tok::Tilde)) {
        n->adverb = a.text;
        take();
    }
    return n;
}

std::vector<NodePtr> Parser::parse() {
    std::vector<NodePtr> out;
    while (cur().kind != Tok::Eof) {
        try {
            if (cur().kind == Tok::Newline || cur().kind == Tok::Dedent) {
                take();
                continue;
            }
            out.push_back(statement());
        } catch (Abort&) {
            // resync at the end of the line
            while (cur().kind != Tok::Newline && cur().kind != Tok::Eof) take();
        }
    }
    return out;
}

NodePtr Parser::statement() {
    if (cur().kind == Tok::Indent) fail("unexpected indent, blocks are not parsed yet");
    NodePtr s;
    Tok nk = after().kind;
    if (cur().kind == Tok::Ident &&
        (nk == Tok::Bind || nk == Tok::BindMut || nk == Tok::Assign)) {
        Token name = take();
        Token op = take();
        s = node(NodeKind::Bind, op);
        s->kids.push_back(node(NodeKind::Name, name));
        s->kids.push_back(expr());
    } else {
        s = expr();
    }
    if (cur().kind != Tok::Newline && cur().kind != Tok::Eof)
        fail("did not expect this here");
    return s;
}

// train_ok is false on the right side of a dyad, a bare verb there makes no sense yet
NodePtr Parser::expr(bool train_ok) {
    if (is_verb(cur().kind)) {
        std::vector<NodePtr> vs;
        while (is_verb(cur().kind)) vs.push_back(verb_node(NodeKind::Verb));
        if (!starts_atom(cur().kind)) {
            if (!train_ok) fail("expected an expression");
            NodePtr t = node(NodeKind::Train, vs[0]->tok);
            t->kids = std::move(vs);
            return t;
        }
        NodePtr rest = expr(false);
        // innermost is the rightmost verb
        for (size_t k = vs.size(); k-- > 0;) {
            vs[k]->kind = NodeKind::Monad;
            vs[k]->kids.push_back(std::move(rest));
            rest = std::move(vs[k]);
        }
        return rest;
    }
    NodePtr left = strand();
    if (is_verb(cur().kind)) {
        NodePtr d = verb_node(NodeKind::Dyad);
        d->kids.push_back(std::move(left));
        d->kids.push_back(expr(false));
        return d;
    }
    return left;
}

// atoms next to each other. could be a list of numbers or a call, sema decides
NodePtr Parser::strand() {
    std::vector<NodePtr> items;
    while (starts_atom(cur().kind)) items.push_back(atom());
    if (items.empty()) fail("expected an expression");
    if (items.size() == 1) return std::move(items[0]);
    NodePtr j = node(NodeKind::Juxt, items[0]->tok);
    j->kids = std::move(items);
    return j;
}

NodePtr Parser::atom() {
    switch (cur().kind) {
        case Tok::IntLit: return node(NodeKind::Int, take());
        case Tok::StrLit: return node(NodeKind::Str, take());
        case Tok::Ident:  return node(NodeKind::Name, take());
        case Tok::LParen: {
            take();
            NodePtr e = expr();
            expect(Tok::RParen, "expected ')'");
            return e;
        }
        case Tok::LBracket: return list();
        default: fail("expected an expression");
    }
}

// [1,2,3]  [1..]  [1,3..]  [1..10]
NodePtr Parser::list() {
    NodePtr l = node(NodeKind::List, take());
    while (true) {
        l->kids.push_back(expr());
        if (cur().kind != Tok::Comma) break;
        take();
    }
    if (cur().kind == Tok::DotDot) {
        take();
        l->dots = true;
        if (cur().kind != Tok::RBracket) {
            l->kids.push_back(expr());
            l->has_end = true;
        }
    }
    expect(Tok::RBracket, "expected ']'");
    return l;
}

}  // namespace rill
