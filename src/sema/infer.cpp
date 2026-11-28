#include "sema/infer.hpp"
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "common/num.hpp"
#include "sema/types.hpp"

namespace rill {
namespace {

enum class K { Var, Int, Bool, Str, Any, Fn, List, Stream };

struct T {
    K k = K::Any;
    bool sign = true;
    int width = 0;
    bool num = false;     // Var that came from a number, can only become an int
    int link = -1;        // Var: what it got resolved to
    int elem = -1;        // List, Stream: type of the items
    std::vector<int> fn;  // Fn: parameter types, then the result
};

struct Lit {
    const Token* tok;
    u128 v;
    int ty;
};

bool arith_tok(Tok k) {
    switch (k) {
        case Tok::Plus: case Tok::Minus: case Tok::Star: case Tok::Slash:
        case Tok::Percent: case Tok::Amp: case Tok::Power: case Tok::Shl: case Tok::Shr:
            return true;
        default:
            return false;
    }
}

class Infer {
public:
    explicit Infer(Diagnostics& d) : diags_(d) {
        i64_ = mk(K::Int, true, 64);
        any_ = mk(K::Any);
        str_ = mk(K::Str);
        bool_ = mk(K::Bool);
    }

    std::vector<std::string> run(std::vector<NodePtr>& program) {
        stmts(program);
        finish();
        std::vector<std::string> lines;
        for (auto& n : program) {
            if (n->kind == NodeKind::Bind && n->tok.kind != Tok::Assign) {
                const Node& x = *n->kids[0];
                lines.push_back(std::string(x.tok.text) + " : " + show(ty_of(x.decl)));
            } else if (n->kind == NodeKind::Def || n->kind == NodeKind::Isomorph) {
                lines.push_back(std::string(n->tok.text) + " : " + show(fns_.at(n->decl)));
            } else if (n->kind == NodeKind::Orbit) {
                lines.push_back(std::string(n->tok.text) + " : " + show(ty_of(n->decl)));
            }
        }
        return lines;
    }

private:
    void err(const Token& t, const std::string& msg) {
        diags_.error(t.line, t.col, t.text.empty() ? 1 : static_cast<int>(t.text.size()), msg);
    }

    int mk(K k, bool sign = true, int width = 0) {
        T t;
        t.k = k;
        t.sign = sign;
        t.width = width;
        ts_.push_back(std::move(t));
        return static_cast<int>(ts_.size()) - 1;
    }

    int var(bool num) {
        int t = mk(K::Var);
        ts_[t].num = num;
        return t;
    }

    int container(K k, int elem) {
        int t = mk(k);
        ts_[t].elem = elem;
        return t;
    }

    int find(int t) {
        while (ts_[t].k == K::Var && ts_[t].link >= 0) t = ts_[t].link;
        return t;
    }

    bool is_cont(int t) {
        K k = ts_[find(t)].k;
        return k == K::List || k == K::Stream;
    }

    bool occurs(int v, int t) {
        t = find(t);
        if (t == v) return true;
        const T& x = ts_[t];
        if (x.k == K::List || x.k == K::Stream) return occurs(v, x.elem);
        if (x.k == K::Fn)
            for (int p : x.fn)
                if (occurs(v, p)) return true;
        return false;
    }

    bool unify(int a, int b) {
        a = find(a);
        b = find(b);
        if (a == b) return true;
        K ka = ts_[a].k, kb = ts_[b].k;
        if (ka == K::Any || kb == K::Any) {
            if (ka == K::Var) ts_[a].link = b;
            else if (kb == K::Var) ts_[b].link = a;
            return true;
        }
        if (ka != K::Var && kb == K::Var) {
            std::swap(a, b);
            std::swap(ka, kb);
        }
        if (ka == K::Var) {
            if (kb == K::Var) {
                if (ts_[a].num) ts_[b].num = true;
                ts_[a].link = b;
                return true;
            }
            if (ts_[a].num && kb != K::Int) return false;
            if (occurs(a, b)) return false;
            ts_[a].link = b;
            return true;
        }
        if (ka != kb) return false;
        if (ka == K::Int) return ts_[a].sign == ts_[b].sign && ts_[a].width == ts_[b].width;
        if (ka == K::Fn) return false;
        if (ka == K::List || ka == K::Stream) return unify(ts_[a].elem, ts_[b].elem);
        return true;  // Bool, Str
    }

    std::string show(int t) {
        t = find(t);
        const T& x = ts_[t];
        switch (x.k) {
            case K::Var:    return x.num ? "number" : "unknown";
            case K::Int:    return std::string(x.sign ? "i" : "u") + std::to_string(x.width);
            case K::Bool:   return "bool";
            case K::Str:    return "str";
            case K::Any:    return "any";
            case K::List:   return "[" + show(x.elem) + "]";
            case K::Stream: return "[" + show(x.elem) + "..]";
            case K::Fn: {
                std::string s;
                for (size_t i = 0; i + 1 < x.fn.size(); ++i) s += show(x.fn[i]) + " ";
                return s + "=> " + show(x.fn.back());
            }
        }
        return "?";
    }

    // i64, [i64] or [i64..]
    int from_ref(const Token& t, int form) {
        int base = from_name(t);
        if (form == 1) return container(K::List, base);
        if (form == 2) return container(K::Stream, base);
        return base;
    }

    int from_name(const Token& t) {
        TyName tn = parse_ty_name(t.text);
        switch (tn.kind) {
            case TyKind::Int:     return mk(K::Int, tn.sign, tn.width);
            case TyKind::Bool:    return bool_;
            case TyKind::Str:     return str_;
            case TyKind::NotType: break;
        }
        return any_;
    }

    int ty_of(int decl) {
        auto [it, fresh] = decl_ty_.try_emplace(decl, -1);
        if (fresh) it->second = var(false);
        return it->second;
    }

    // a block's errors point at its last line, the block token itself is a newline
    const Token& where(const Node& n) {
        if (n.kind == NodeKind::Block && !n.kids.empty()) return where(*n.kids.back());
        return n.tok;
    }

    // item of a list or strand, has to match the ones before it
    void same(int& first, Node& item) {
        int t = ex(item);
        if (is_cont(t)) {
            err(where(item), "lists of lists are not supported yet");
            if (first < 0) first = any_;
            return;
        }
        if (first < 0) { first = t; return; }
        if (!unify(first, t))
            err(where(item), "list items need one type, got " + show(first) + " and " + show(t));
    }

    int stmts(std::vector<NodePtr>& nodes) {
        hoist(nodes);
        int last = any_;
        for (auto& n : nodes) last = ex(*n);
        return last;
    }

    void hoist(std::vector<NodePtr>& nodes) {
        for (auto& n : nodes) {
            if (n->kind != NodeKind::Def && n->kind != NodeKind::Isomorph) continue;
            std::vector<int> sig;
            if (n->sig) {
                for (auto& k : n->sig->kids) sig.push_back(from_ref(k->tok, k->tyform));
            } else {
                for (int i = 0; i <= n->arity; ++i) sig.push_back(var(false));
            }
            int f = mk(K::Fn);
            ts_[f].fn = std::move(sig);
            fns_[n->decl] = f;
        }
    }

    // every expression goes through here so its final type can be copied onto the node
    int ex(Node& n) {
        int t = ex_(n);
        // a def gets its result type in def(), this one would overwrite it
        if (n.kind != NodeKind::Def && n.kind != NodeKind::Isomorph) seen_.push_back({&n, t});
        return t;
    }

    TySlot summarize(int t) {
        const T& x = ts_[find(t)];
        TySlot s;
        switch (x.k) {
            case K::Int:  s.k = TK::Int; s.sign = x.sign; s.width = x.width; break;
            case K::Bool: s.k = TK::Bool; break;
            case K::Str:  s.k = TK::Str; break;
            default:      s.k = TK::Other; break;
        }
        return s;
    }

    int ex_(Node& n) {
        switch (n.kind) {
            case NodeKind::Int: {
                int t = var(true);
                u128 v = 0;
                if (parse_u128(n.tok.text, v)) lits_.push_back({&n.tok, v, t});
                return t;
            }
            case NodeKind::Str:      return str_;
            case NodeKind::Bool:     return bool_;
            case NodeKind::Name:     return use_name(n);
            case NodeKind::Bind:     return bind(n);
            case NodeKind::Monad:    return monad(n);
            case NodeKind::Dyad:     return dyad(n);
            case NodeKind::Juxt:     return juxt(n);
            case NodeKind::List:     return list(n);
            case NodeKind::Block:    return stmts(n.kids);
            case NodeKind::Match:    return match(n);
            case NodeKind::Def:
            case NodeKind::Isomorph: def(n); return any_;
            case NodeKind::Orbit:    orbit(n); return any_;
            case NodeKind::Manifold: ex(*n.kids[1]); ex(*n.kids[2]); return any_;
            case NodeKind::Fixpoint:
            case NodeKind::Quiesce:  ex(*n.kids[0]); return any_;
            default: return any_;  // Verb, Train, Lattice, Sig
        }
    }

    int use_name(Node& n) {
        switch (n.sym) {
            case Sym::Val:
            case Sym::Mut:
            case Sym::Arg:
            case Sym::Stream: return ty_of(n.decl);
            default:          return any_;
        }
    }

    int bind(Node& n) {
        Node& name = *n.kids[0];
        Node& rhs = *n.kids[1];
        int r = ex(rhs);
        if (name.sym == Sym::Func) {  // a bound train
            decl_ty_[name.decl] = any_;
            return any_;
        }
        int x = ty_of(name.decl);
        if (n.annotated) unify(x, from_ref(n.ann, n.annform));
        if (!unify(x, r))
            err(where(rhs), "'" + std::string(name.tok.text) + "' has type " + show(x) +
                            " but the value is " + show(r));
        return x;
    }

    // [1,2,3] is a list, [1,3..] a stream, [1..10] a list again
    int list(Node& n) {
        size_t items = n.kids.size() - (n.has_end ? 1 : 0);
        int first = -1;
        for (size_t i = 0; i < items; ++i) same(first, *n.kids[i]);
        if (!n.dots) return container(K::List, first);

        if (!unify(first, var(true)))
            err(n.tok, "a range needs numbers, got " + show(first));
        if (n.has_end) {
            Node& end = *n.kids.back();
            int e = ex(end);
            if (!unify(first, e))
                err(where(end), "the end of the range is " + show(e) + " but the start is " + show(first));
            return container(K::List, first);
        }
        return container(K::Stream, first);
    }

    int monad(Node& n) {
        int t = ex(*n.kids[0]);
        Tok op = n.tok.kind;
        if (n.adverb == "/") return fold(n, t);
        if (!n.adverb.empty()) {
            // *~ squares, so the shape that goes in comes out
            if (n.adverb == "~" && arith_tok(op)) return t;
            return any_;
        }
        if (op == Tok::Minus) return t;
        if (op == Tok::Hash) {
            if (ts_[find(t)].k == K::Stream)
                err(n.tok, "a stream has no length, take some first, like 10#");
            return i64_;
        }
        return any_;
    }

    int fold(Node& n, int t) {
        if (!arith_tok(n.tok.kind)) return any_;
        int f = find(t);
        switch (ts_[f].k) {
            case K::Stream:
                err(n.tok, "can't fold an infinite stream, take some first, like 10#");
                return ts_[f].elem;
            case K::List:
                return ts_[f].elem;
            case K::Var: {
                if (ts_[f].num) return f;
                // unknown, a fold is most likely over a list
                int e = var(false);
                unify(f, container(K::List, e));
                return e;
            }
            case K::Any:
            case K::Fn:
                return any_;
            default:
                return f;  // folding one value gives the value
        }
    }

    int dyad(Node& n) {
        int l = ex(*n.kids[0]);
        int r = ex(*n.kids[1]);
        if (!n.adverb.empty()) return any_;
        switch (n.tok.kind) {
            case Tok::Hash: return take(n, l, r);
            case Tok::At:   return index(n, l, r);
            case Tok::Lt: case Tok::Gt: case Tok::Eq:
                return arith(n, l, r, true);
            default:
                if (arith_tok(n.tok.kind)) return arith(n, l, r, false);
                return any_;
        }
    }

    int arith(Node& n, int l, int r, bool compare) {
        int a = find(l), b = find(r);
        K ka = ts_[a].k, kb = ts_[b].k;
        if (ka == K::Any || kb == K::Any) return any_;
        bool ca = ka == K::List || ka == K::Stream;
        bool cb = kb == K::List || kb == K::Stream;
        std::string op(n.tok.text);

        if (!ca && !cb) {
            if (!unify(a, b))
                err(n.tok, "'" + op + "' needs both sides the same type, got " + show(a) + " and " + show(b));
            return compare ? bool_ : a;
        }
        if (ca && cb) {
            if (ka != kb) {
                err(n.tok, "'" + op + "' can't mix a list and a stream, take from the stream first");
                return any_;
            }
            if (!unify(ts_[a].elem, ts_[b].elem))
                err(n.tok, "'" + op + "' needs both sides the same type, got " + show(a) + " and " + show(b));
            return container(ka, compare ? bool_ : ts_[a].elem);
        }
        // one side is a list or stream, the other is not
        int c = ca ? a : b;
        int o = ca ? b : a;
        K kc = ts_[c].k;
        if (ts_[o].k == K::Var && !ts_[o].num) {
            // don't know what it is yet, assume the same shape
            unify(o, c);
            return compare ? container(kc, bool_) : c;
        }
        int e = ts_[c].elem;
        if (!unify(e, o))
            err(n.tok, "'" + op + "' needs both sides the same type, got " + show(a) + " and " + show(b));
        return container(kc, compare ? bool_ : e);
    }

    // n # x
    int take(Node& n, int l, int r) {
        int a = find(l), b = find(r);
        K ka = ts_[a].k;
        if (ka == K::List || ka == K::Stream || ka == K::Bool || ka == K::Str)
            err(n.tok, "the count before # must be one number, got " + show(a));
        else if (ka == K::Var && !ts_[a].num)
            unify(a, var(true));

        K kb = ts_[b].k;
        if (kb == K::List || kb == K::Stream) return container(K::List, ts_[b].elem);
        if (kb == K::Any || kb == K::Fn || (kb == K::Var && !ts_[b].num)) return any_;
        return container(K::List, b);  // one value, repeated
    }

    // x @ i
    int index(Node& n, int l, int r) {
        int a = find(l);
        if (is_cont(r)) err(n.tok, "the index must be one number");
        K ka = ts_[a].k;
        if (ka == K::List || ka == K::Stream) return ts_[a].elem;
        if (ka == K::Any || ka == K::Str || ka == K::Fn || (ka == K::Var && !ts_[a].num)) return any_;
        err(n.tok, "'@' needs a list on the left, got " + show(a));
        return any_;
    }

    int juxt(Node& n) {
        Node& head = *n.kids[0];
        auto f = head.kind == NodeKind::Name ? fns_.find(head.decl) : fns_.end();
        bool known = f != fns_.end();
        if (n.shape == Shape::Call && known) return call(n, f->second);
        if (n.shape == Shape::CallStrand && known) return call_strand(n, f->second);

        int first = -1;
        if (n.shape == Shape::List) {
            for (auto& k : n.kids) same(first, *k);
            return container(K::List, first);
        }
        if (n.shape == Shape::CallStrand) {
            ex(head);
            for (size_t i = 1; i < n.kids.size(); ++i) same(first, *n.kids[i]);
            return any_;
        }
        for (auto& k : n.kids) ex(*k);
        return any_;
    }

    int call(Node& n, int fn) {
        std::vector<int> sig = ts_[fn].fn;  // copy, ts_ can grow while we walk the args
        for (size_t i = 1; i < n.kids.size(); ++i) {
            int a = ex(*n.kids[i]);
            if (i < sig.size() && !unify(sig[i - 1], a))
                err(where(*n.kids[i]), "'" + std::string(n.kids[0]->tok.text) + "' takes " +
                                       show(sig[i - 1]) + " here, got " + show(a));
        }
        return sig.back();
    }

    // sq 1 2, a one-argument function given a strand
    int call_strand(Node& n, int fn) {
        std::vector<int> sig = ts_[fn].fn;
        int first = -1;
        for (size_t i = 1; i < n.kids.size(); ++i) same(first, *n.kids[i]);
        int lst = container(K::List, first);
        if (!unify(sig[0], lst))
            err(where(*n.kids[1]), "'" + std::string(n.kids[0]->tok.text) + "' takes " +
                                   show(sig[0]) + " here, got " + show(lst));
        return sig.back();
    }

    void pattern(Node& p, int want) {
        int t;
        if (p.kind == NodeKind::Name) {
            if (p.tok.text == "_") { seen_.push_back({&p, want}); return; }
            t = ty_of(p.decl);
            seen_.push_back({&p, t});
        } else {
            t = ex(p);
        }
        if (!unify(want, t))
            err(p.tok, "this pattern is " + show(t) + " but the value is " + show(want));
    }

    void def(Node& n) {
        std::vector<int> sig = ts_[fns_.at(n.decl)].fn;
        int ret = sig.back();
        seen_.push_back({&n, ret});
        for (auto& c : n.kids) {
            for (size_t i = 0; i + 1 < c->kids.size(); ++i) pattern(*c->kids[i], sig[i]);
            Node& body = *c->kids.back();
            int b = ex(body);
            if (!unify(ret, b))
                err(where(body), "'" + std::string(n.tok.text) + "' gives " + show(ret) +
                                 " but this gives " + show(b));
        }
    }

    int match(Node& n) {
        int s = ex(*n.kids[0]);
        int result = -1;
        for (size_t i = 1; i < n.kids.size(); ++i) {
            Node& arm = *n.kids[i];
            int want = arm.kids.size() == 2 ? s : any_;
            for (size_t j = 0; j + 1 < arm.kids.size(); ++j) pattern(*arm.kids[j], want);
            Node& body = *arm.kids.back();
            int b = ex(body);
            if (result < 0)
                result = b;
            else if (!unify(result, b))
                err(where(body), "this arm gives " + show(b) + " but the earlier ones give " + show(result));
        }
        return result < 0 ? any_ : result;
    }

    void orbit(Node& n) {
        Node& seed = *n.kids[0];
        Node& state = *n.kids[1];
        Node& step = *n.kids[2];
        std::vector<Node*> names;
        if (state.kind == NodeKind::Name) names.push_back(&state);
        else for (auto& k : state.kids) names.push_back(k.get());

        // with more than one state value, seed and step are written as strands
        auto parts = [&](Node& x) {
            std::vector<Node*> v;
            if (names.size() > 1 && x.kind == NodeKind::Juxt) for (auto& k : x.kids) v.push_back(k.get());
            else v.push_back(&x);
            return v;
        };
        std::vector<Node*> seeds = parts(seed);
        std::vector<Node*> steps = parts(step);
        if (steps.size() != names.size()) {
            err(step.tok, "the step has " + std::to_string(steps.size()) +
                          " values but the state has " + std::to_string(names.size()) + " names");
        } else {
            for (size_t i = 0; i < names.size(); ++i) {
                int s = names[i]->tok.text == "_" ? any_ : ty_of(names[i]->decl);
                int a = ex(*seeds[i]);
                if (!unify(s, a))
                    err(where(*seeds[i]), "the seed is " + show(a) + " but the state is " + show(s));
                else if (is_cont(s))
                    err(where(*seeds[i]), "an orbit state is one value, not a list");
                int b = ex(*steps[i]);
                if (!unify(s, b))
                    err(where(*steps[i]), "the step gives " + show(b) + " but the state is " + show(s));
            }
        }
        // the stream hands out the first state value
        int s0 = names[0]->tok.text == "_" ? any_ : ty_of(names[0]->decl);
        if (n.decl >= 0) decl_ty_[n.decl] = container(K::Stream, s0);
    }

    // anything still unknown becomes i64, then every number gets its fit check
    void finish() {
        for (size_t i = 0; i < ts_.size(); ++i)
            if (ts_[i].k == K::Var && ts_[i].link < 0) ts_[i].link = i64_;
        for (auto& s : seen_) s.first->ty = summarize(s.second);
        for (const Lit& l : lits_) {
            const T& t = ts_[find(l.ty)];
            if (t.k != K::Int) continue;
            int bits = t.sign ? t.width - 1 : t.width;
            if (bits < 128 && (l.v >> bits) != 0)
                err(*l.tok, std::string(l.tok->text) + " does not fit in " + show(l.ty));
        }
    }

    Diagnostics& diags_;
    std::vector<T> ts_;
    std::unordered_map<int, int> decl_ty_;  // declaration id -> type
    std::unordered_map<int, int> fns_;      // declaration id of a def -> its Fn type
    std::vector<Lit> lits_;
    std::vector<std::pair<Node*, int>> seen_;
    int i64_ = 0, any_ = 0, str_ = 0, bool_ = 0;
};

}  // namespace

std::vector<std::string> infer(std::vector<NodePtr>& program, Diagnostics& diags) {
    return Infer(diags).run(program);
}

}  // namespace rill
