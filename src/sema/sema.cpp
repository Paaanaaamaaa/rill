#include "sema/sema.hpp"
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include "common/num.hpp"
#include "sema/types.hpp"

namespace rill {
namespace {

struct Entry {
    Sym sym;
    int arity;  // -1 means any, that is what a bound train gets
    int id;     // one per declaration, infer uses it to find a name's type
};

class Sema {
public:
    explicit Sema(Diagnostics& d) : diags_(d) { scopes_.emplace_back(); }
    void run(std::vector<NodePtr>& program) { stmts(program); }

private:
    using Scope = std::unordered_map<std::string_view, Entry>;

    void err(const Token& t, const std::string& msg) {
        diags_.error(t.line, t.col, t.text.empty() ? 1 : static_cast<int>(t.text.size()), msg);
    }

    void push() { scopes_.emplace_back(); }
    void pop() { scopes_.pop_back(); }

    // gives back the new id, or -1 when the name can't be declared
    int declare(const Token& t, Sym s, int arity) {
        if (parse_ty_name(t.text).kind != TyKind::NotType) {
            err(t, "'" + std::string(t.text) + "' is a type name, pick another name");
            return -1;
        }
        auto [it, fresh] = scopes_.back().emplace(t.text, Entry{s, arity, next_id_});
        if (!fresh) {
            err(t, "'" + std::string(t.text) + "' is already defined here");
            return -1;
        }
        return next_id_++;
    }

    const Entry* lookup(std::string_view name) const {
        for (size_t i = scopes_.size(); i-- > 0;) {
            auto it = scopes_[i].find(name);
            if (it != scopes_[i].end()) return &it->second;
        }
        return nullptr;
    }

    static bool is_def(const Node& n) {
        return n.kind == NodeKind::Def || n.kind == NodeKind::Isomorph;
    }

    // functions and types can be used before the line that defines them
    void hoist(std::vector<NodePtr>& nodes) {
        for (auto& n : nodes) {
            if (is_def(*n)) {
                n->arity = static_cast<int>(n->kids[0]->kids.size()) - 1;
                n->decl = declare(n->tok, Sym::Func, n->arity);
            } else if (n->kind == NodeKind::Lattice) {
                n->decl = declare(n->tok, Sym::Type, -1);
            }
        }
        // a signature belongs to the definition with the same name in the same block
        for (auto& n : nodes) {
            if (n->kind != NodeKind::Sig) continue;
            Node* def = nullptr;
            for (auto& m : nodes)
                if (is_def(*m) && m->tok.text == n->tok.text) { def = m.get(); break; }
            std::string name(n->tok.text);
            int params = static_cast<int>(n->kids.size()) - 1;
            if (!def)
                err(n->tok, "no definition of '" + name + "' in this block");
            else if (def->sig)
                err(n->tok, "'" + name + "' already has a signature");
            else if (params != def->arity)
                err(n->tok, "the signature has " + std::to_string(params) + " types but '" +
                            name + "' takes " + std::to_string(def->arity));
            else
                def->sig = n.get();
        }
    }

    void stmts(std::vector<NodePtr>& nodes) {
        hoist(nodes);
        for (auto& n : nodes) visit(*n);
    }

    void visit_kids(Node& n) {
        for (auto& k : n.kids) visit(*k);
    }

    void visit(Node& n) {
        switch (n.kind) {
            case NodeKind::Int:      check_int(n); break;
            case NodeKind::Name:     resolve(n); break;
            case NodeKind::Bind:     binding(n); break;
            case NodeKind::Juxt:     visit_kids(n); shape(n); break;
            case NodeKind::Def:
            case NodeKind::Isomorph: def(n); break;
            case NodeKind::Block:    push(); stmts(n.kids); pop(); break;
            case NodeKind::Match:    match(n); break;
            case NodeKind::Orbit:    orbit(n); break;
            case NodeKind::Manifold: manifold(n); break;
            case NodeKind::Lattice:  lattice(n); break;
            case NodeKind::Sig:
                for (auto& k : n.kids) check_type(k->tok);
                break;
            case NodeKind::Monad:
            case NodeKind::Dyad:
            case NodeKind::List:
            case NodeKind::Fixpoint:
            case NodeKind::Quiesce:  visit_kids(n); break;
            default: break;  // Str, Verb, Train
        }
    }

    void check_type(const Token& t) {
        TyName tn = parse_ty_name(t.text);
        if (tn.kind == TyKind::NotType)
            err(t, "'" + std::string(t.text) + "' is not a type, try i64 or u8");
        else if (tn.bad_width)
            err(t, "bit width must be between 1 and 128");
    }

    void resolve(Node& n) {
        if (n.tok.text == "_") {
            err(n.tok, "'_' only works as a pattern");
            return;
        }
        const Entry* e = lookup(n.tok.text);
        if (!e) {
            err(n.tok, "unknown name '" + std::string(n.tok.text) + "'");
            return;
        }
        n.sym = e->sym;
        n.arity = e->arity;
        n.decl = e->id;
    }

    void binding(Node& n) {
        Node& name = *n.kids[0];
        Node& rhs = *n.kids[1];
        visit(rhs);  // right side first, so  x <~ x  means the outer x
        if (n.annotated) check_type(n.ann);
        if (n.tok.kind == Tok::Assign) {
            const Entry* e = lookup(name.tok.text);
            if (!e) {
                err(name.tok, "unknown name '" + std::string(name.tok.text) + "'");
            } else if (e->sym != Sym::Mut) {
                err(name.tok, "'" + std::string(name.tok.text) + "' is not mutable, declare it with <~!");
            } else {
                name.sym = Sym::Mut;
                name.decl = e->id;
            }
            return;
        }
        Sym s = rhs.kind == NodeKind::Train ? Sym::Func
              : n.tok.kind == Tok::BindMut ? Sym::Mut : Sym::Val;
        name.decl = declare(name.tok, s, -1);
        name.sym = s;
    }

    // patterns are the first `count` kids of a clause or arm
    void bind_patterns(Node& owner, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            Node& p = *owner.kids[i];
            if (p.kind == NodeKind::Int) {
                check_int(p);
            } else if (p.kind == NodeKind::Name && p.tok.text != "_") {
                p.decl = declare(p.tok, Sym::Arg, -1);
                p.sym = Sym::Arg;
            }
        }
    }

    void def(Node& n) {
        for (auto& c : n.kids) {
            int got = static_cast<int>(c->kids.size()) - 1;
            if (got != n.arity)
                err(c->tok, "this clause has " + std::to_string(got) +
                            " patterns, the first one has " + std::to_string(n.arity));
            push();
            bind_patterns(*c, c->kids.size() - 1);
            visit(*c->kids.back());
            pop();
        }
    }

    void match(Node& n) {
        visit(*n.kids[0]);
        int want = -1;
        for (size_t i = 1; i < n.kids.size(); ++i) {
            Node& arm = *n.kids[i];
            int got = static_cast<int>(arm.kids.size()) - 1;
            if (want < 0) want = got;
            else if (got != want)
                err(arm.tok, "this arm has " + std::to_string(got) +
                             " patterns, the first one has " + std::to_string(want));
            push();
            bind_patterns(arm, arm.kids.size() - 1);
            visit(*arm.kids.back());
            pop();
        }
    }

    void orbit(Node& n) {
        Node& seed = *n.kids[0];
        Node& state = *n.kids[1];
        Node& step = *n.kids[2];
        visit(seed);

        std::vector<Node*> names;
        if (state.kind == NodeKind::Name) {
            names.push_back(&state);
        } else if (state.kind == NodeKind::Juxt) {
            for (auto& k : state.kids) {
                if (k->kind == NodeKind::Name) names.push_back(k.get());
                else err(k->tok, "state can only hold plain names");
            }
        } else {
            err(state.tok, "state must be a name or names like (a b)");
        }

        size_t seeds = seed.kind == NodeKind::Juxt ? seed.kids.size() : 1;
        if (!names.empty() && seeds != names.size())
            err(seed.tok, "the seed has " + std::to_string(seeds) +
                          " values but the state has " + std::to_string(names.size()) + " names");

        push();
        for (Node* s : names) {
            if (s->tok.text == "_") continue;
            s->decl = declare(s->tok, Sym::Arg, -1);
            s->sym = Sym::Arg;
        }
        visit(step);
        pop();

        // with several state values the three parts are tuples, not calls
        if (names.size() > 1) {
            for (Node* part : {&seed, &state, &step})
                if (part->kind == NodeKind::Juxt) part->shape = Shape::List;
        }

        n.decl = declare(n.tok, Sym::Stream, -1);
        n.sym = Sym::Stream;
    }

    void manifold(Node& n) {
        visit(*n.kids[1]);
        push();
        n.kids[0]->decl = declare(n.kids[0]->tok, Sym::Region, -1);
        n.kids[0]->sym = Sym::Region;
        visit(*n.kids[2]);
        pop();
    }

    void lattice(Node& n) {
        std::unordered_set<std::string_view> seen;
        for (auto& f : n.kids) {
            if (!seen.insert(f->tok.text).second)
                err(f->tok, "field '" + std::string(f->tok.text) + "' appears twice");
            const Token& w = f->kids[0]->tok;
            u128 v;
            if (!parse_u128(w.text, v) || v < 1 || v > 128)
                err(w, "bit width must be between 1 and 128");
        }
    }

    // only checks the size here, which type a number gets is up to infer
    void check_int(Node& n) {
        u128 v;
        if (!parse_u128(n.tok.text, v))
            err(n.tok, "number is too big, 128 bits is the limit");
    }

    // decide if `a b c` is a call or just values side by side
    void shape(Node& n) {
        Node& head = *n.kids[0];
        int args = static_cast<int>(n.kids.size()) - 1;
        int arity = -1;
        bool callable = false;
        if (head.kind == NodeKind::Train) {
            callable = true;
        } else if (head.kind == NodeKind::Name) {
            if (head.sym == Sym::Func) {
                callable = true;
                arity = head.arity;
            } else if (head.sym == Sym::Arg) {
                n.shape = Shape::Unknown;  // might be a function, needs types
                return;
            }
        }
        if (!callable) {
            n.shape = Shape::List;
        } else if (arity < 0) {
            n.shape = args == 1 ? Shape::Call : Shape::CallStrand;
        } else if (args == arity) {
            n.shape = Shape::Call;
        } else if (arity == 1) {
            n.shape = Shape::CallStrand;
        } else {
            err(head.tok, "'" + std::string(head.tok.text) + "' takes " + std::to_string(arity) +
                          " arguments but got " + std::to_string(args));
        }
    }

    Diagnostics& diags_;
    std::vector<Scope> scopes_;
    int next_id_ = 0;
};

}  // namespace

void analyze(std::vector<NodePtr>& program, Diagnostics& diags) {
    Sema(diags).run(program);
}

}  // namespace rill
