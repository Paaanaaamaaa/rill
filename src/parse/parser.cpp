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
    return k == Tok::IntLit || k == Tok::StrLit || k == Tok::BoolLit || k == Tok::Ident ||
           k == Tok::LParen || k == Tok::LBracket;
}

bool prefix_kw(Tok k) { return k == Tok::KwQuiesce || k == Tok::KwFixpoint; }

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

void Parser::end_line() {
    if (ate_newline_) { ate_newline_ = false; return; }
    if (cur().kind != Tok::Newline && cur().kind != Tok::Eof)
        fail("did not expect this here");
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
            ate_newline_ = false;
            // resync at the end of the line
            while (cur().kind != Tok::Newline && cur().kind != Tok::Eof) take();
        }
    }
    return out;
}

NodePtr Parser::statement() {
    if (cur().kind == Tok::Indent) fail("unexpected indent, blocks are not parsed yet");
    switch (cur().kind) {
        case Tok::KwOrbit:    return orbit();
        case Tok::KwIsomorph: return isomorph();
        case Tok::KwLattice:  return lattice();
        case Tok::KwManifold: return manifold();
        default: break;
    }
    if (cur().kind == Tok::Ident && after().kind == Tok::Colon) return annotated();
    if (cur().kind == Tok::Ident && after().kind == Tok::Clause) return definition(NodeKind::Def);
    if (cur().kind == Tok::Clause) fail("this :: has no name above it");
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
    end_line();
    return s;
}

// train_ok is false on the right side of a dyad, a bare verb there makes no sense yet
NodePtr Parser::expr(bool train_ok) {
    if (prefix_kw(cur().kind)) {
        Tok k = cur().kind;
        NodePtr n = node(k == Tok::KwQuiesce ? NodeKind::Quiesce : NodeKind::Fixpoint, take());
        n->kids.push_back(expr(false));
        return n;
    }
    if (is_verb(cur().kind)) {
        std::vector<NodePtr> vs;
        while (is_verb(cur().kind)) vs.push_back(verb_node(NodeKind::Verb));
        if (!starts_atom(cur().kind) && !prefix_kw(cur().kind)) {
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
    // ?| grabs the noun right before it, same as every other verb
    if (cur().kind == Tok::Match) return match(std::move(left));
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
        case Tok::BoolLit: return node(NodeKind::Bool, take());
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

// literals and names only for now
void Parser::patterns(Node& into) {
    while (true) {
        Tok k = cur().kind;
        if (k == Tok::IntLit) into.kids.push_back(node(NodeKind::Int, take()));
        else if (k == Tok::StrLit) into.kids.push_back(node(NodeKind::Str, take()));
        else if (k == Tok::BoolLit) into.kids.push_back(node(NodeKind::Bool, take()));
        else if (k == Tok::Ident) into.kids.push_back(node(NodeKind::Name, take()));
        else break;
    }
}

// name :: pats => body, then more clauses on indented lines
NodePtr Parser::definition(NodeKind kind) {
    Token name = take();
    NodePtr def = node(kind, name);
    def->kids.push_back(clause());
    if (cur().kind == Tok::Newline && after().kind == Tok::Indent) {
        take();
        take();
        while (true) {
            def->kids.push_back(clause());
            if (cur().kind == Tok::Newline) take();
            if (cur().kind == Tok::Dedent) { take(); break; }
        }
    }
    ate_newline_ = false;
    return def;
}

NodePtr Parser::clause() {
    Token start = cur();
    expect(Tok::Clause, "expected '::'");
    NodePtr c = node(NodeKind::Clause, start);
    patterns(*c);
    expect(Tok::FatArrow, "expected '=>' after the patterns");
    c->kids.push_back(clause_body());
    return c;
}

NodePtr Parser::clause_body() {
    if (cur().kind != Tok::Newline) return expr();
    Token nl = take();
    if (cur().kind != Tok::Indent) fail("expected an indented block");
    take();
    NodePtr b = node(NodeKind::Block, nl);
    while (cur().kind != Tok::Dedent) {
        if (cur().kind == Tok::Eof) fail("block was never closed");
        if (cur().kind == Tok::Newline) { take(); continue; }
        b->kids.push_back(statement());
    }
    take();
    return b;
}

// subject ?| pats => body | pats => body
// more arms can go on following lines starting with |
NodePtr Parser::match(NodePtr subject) {
    NodePtr m = node(NodeKind::Match, take());
    m->kids.push_back(std::move(subject));
    bool indented = false;
    while (true) {
        NodePtr arm = node(NodeKind::Arm, cur());
        patterns(*arm);
        if (arm->kids.empty()) fail("expected a pattern");
        expect(Tok::FatArrow, "expected '=>' after the patterns");
        arm->kids.push_back(expr(false));
        m->kids.push_back(std::move(arm));

        if (cur().kind == Tok::Bar) { take(); continue; }
        if (cur().kind == Tok::Newline && peek(1).kind == Tok::Indent && peek(2).kind == Tok::Bar) {
            take(); take(); take();
            indented = true;
            continue;
        }
        if (cur().kind == Tok::Newline && peek(1).kind == Tok::Bar) {
            take(); take();
            continue;
        }
        break;
    }
    // we opened an indent for the | lines, so the matching dedent is ours
    if (indented && cur().kind == Tok::Newline && peek(1).kind == Tok::Dedent) {
        take(); take();
        ate_newline_ = true;
    }
    return m;
}

// orbit name <~ seed => state => step
// no idea yet what the three parts should be allowed to look like
NodePtr Parser::orbit() {
    take();
    Token name = cur();
    expect(Tok::Ident, "expected a name after orbit");
    NodePtr o = node(NodeKind::Orbit, name);
    expect(Tok::Bind, "expected '<~' after the orbit name");
    o->kids.push_back(expr(false));
    expect(Tok::FatArrow, "expected '=>' after the seed");
    o->kids.push_back(expr(false));
    expect(Tok::FatArrow, "expected '=>' after the state names");
    o->kids.push_back(expr(false));
    end_line();
    return o;
}

NodePtr Parser::isomorph() {
    take();
    if (cur().kind != Tok::Ident || after().kind != Tok::Clause)
        fail("expected 'name ::' after isomorph");
    return definition(NodeKind::Isomorph);
}

// lattice name, then indented lines of  field : width
NodePtr Parser::lattice() {
    take();
    Token name = cur();
    expect(Tok::Ident, "expected a name after lattice");
    NodePtr l = node(NodeKind::Lattice, name);
    expect(Tok::Newline, "expected the fields on indented lines");
    expect(Tok::Indent, "expected an indented list of fields");
    while (cur().kind != Tok::Dedent) {
        if (cur().kind == Tok::Eof) fail("lattice was never closed");
        if (cur().kind == Tok::Newline) { take(); continue; }
        Token f = cur();
        expect(Tok::Ident, "expected a field name");
        expect(Tok::Colon, "expected ':' after the field name");
        NodePtr fld = node(NodeKind::Field, f);
        Token w = cur();
        expect(Tok::IntLit, "expected a bit width");
        fld->kids.push_back(node(NodeKind::Int, w));
        l->kids.push_back(std::move(fld));
    }
    take();
    return l;
}

// manifold name size, then an indented block
NodePtr Parser::manifold() {
    take();
    Token name = cur();
    expect(Tok::Ident, "expected a name after manifold");
    NodePtr m = node(NodeKind::Manifold, name);
    m->kids.push_back(node(NodeKind::Name, name));
    m->kids.push_back(expr(false));
    NodePtr body = clause_body();
    bool block = body->kind == NodeKind::Block;
    m->kids.push_back(std::move(body));
    if (!block) end_line();
    return m;
}

// i64, [i64] or [i64..]
NodePtr Parser::type_ref() {
    int form = 0;
    if (cur().kind == Tok::LBracket) {
        take();
        form = 1;
        if (cur().kind == Tok::LBracket) fail("lists of lists are not supported yet");
    }
    Token t = cur();
    expect(Tok::Ident, "expected a type");
    if (form) {
        if (cur().kind == Tok::DotDot) {
            take();
            form = 2;
        }
        expect(Tok::RBracket, "expected ']'");
    }
    NodePtr n = node(NodeKind::Name, t);
    n->tyform = form;
    return n;
}

NodePtr Parser::annotated() {
    Token name = take();
    take();
    auto starts_type = [&] { return cur().kind == Tok::Ident || cur().kind == Tok::LBracket; };
    std::vector<NodePtr> types;
    while (starts_type()) types.push_back(type_ref());
    if (types.empty()) fail("expected a type after ':'");

    if (cur().kind == Tok::FatArrow) {
        take();
        if (!starts_type()) fail("expected the result type after '=>'");
        NodePtr ret = type_ref();
        NodePtr s = node(NodeKind::Sig, name);
        for (auto& t : types) s->kids.push_back(std::move(t));
        s->kids.push_back(std::move(ret));
        end_line();
        return s;
    }

    if (types.size() != 1) fail("a bind has exactly one type");
    if (cur().kind != Tok::Bind && cur().kind != Tok::BindMut)
        fail("expected '<~' or '=>' after the type");
    Token op = take();
    NodePtr b = node(NodeKind::Bind, op);
    b->kids.push_back(node(NodeKind::Name, name));
    b->kids.push_back(expr());
    b->annotated = true;
    b->ann = types[0]->tok;
    b->annform = types[0]->tyform;
    end_line();
    return b;
}

}  // namespace rill
