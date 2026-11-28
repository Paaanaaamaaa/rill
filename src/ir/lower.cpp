#include "ir/lower.hpp"
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace rill {
namespace {

// thrown to get out of the statement we can't lower, caught in run()
struct Abort {};

bool scalar(const TySlot& s, IrTy& out) {
    if (s.k == TK::Int) {
        out = IrTy{s.width, s.sign, false};
        return true;
    }
    if (s.k == TK::Bool) {
        out = IrTy{1, false, true};
        return true;
    }
    return false;
}

// lit still has its quotes. the lexer already checked the escapes
std::string decode(std::string_view lit) {
    std::string out;
    for (size_t i = 1; i + 1 < lit.size(); ++i) {
        char c = lit[i];
        if (c != '\\') {
            out += c;
            continue;
        }
        char e = lit[++i];
        if (e == 'n') out += '\n';
        else if (e == 't') out += '\t';
        else if (e == '0') out += '\0';
        else out += e;  // handle \" and \\ escapes
    }
    return out;
}

class Lower {
public:
    explicit Lower(Diagnostics& d) : diags_(d) {}

    Module run(std::vector<NodePtr>& program) {
        mod_.fns.emplace_back();
        mod_.fns[0].name = "main";
        find_captured(program);
        // every function exists before any body is lowered, so calls can point forward
        for (auto& n : program) {
            if (n->kind != NodeKind::Def && n->kind != NodeKind::Isomorph) continue;
            fn_index_[n->decl] = static_cast<int>(mod_.fns.size());
            mod_.fns.emplace_back();
            signature(*n, mod_.fns.back());
        }

        cur_ = 0;
        bb_ = new_block();
        for (auto& n : program) {
            NodeKind k = n->kind;
            if (k == NodeKind::Def || k == NodeKind::Isomorph || k == NodeKind::Sig) continue;
            try {
                if (k == NodeKind::Lattice) unsupported(n->tok, "lattice");
                expr(*n);
            } catch (Abort&) {
            }
        }
        int zero = emit_const(IrTy{64, true, false}, 0);
        terminate(Term::Exit, zero);

        for (auto& n : program) {
            if (n->kind != NodeKind::Def && n->kind != NodeKind::Isomorph) continue;
            try {
                def(*n, fn_index_.at(n->decl));
            } catch (Abort&) {
            }
        }
        return std::move(mod_);
    }

private:
    void err(const Token& t, const std::string& msg) {
        diags_.error(t.line, t.col, t.text.empty() ? 1 : static_cast<int>(t.text.size()), msg);
    }

    [[noreturn]] void unsupported(const Token& t, const std::string& what) {
        err(t, "not lowered yet: " + what);
        throw Abort{};
    }

    Function& fn() { return mod_.fns[static_cast<size_t>(cur_)]; }
    Block& blk() { return fn().blocks[static_cast<size_t>(bb_)]; }

    int new_block() {
        fn().blocks.emplace_back();
        return static_cast<int>(fn().blocks.size()) - 1;
    }

    int new_reg(IrTy t) {
        fn().vregs.push_back(t);
        return static_cast<int>(fn().vregs.size()) - 1;
    }

    IrTy reg_ty(int r) { return fn().vregs[static_cast<size_t>(r)]; }

    int emit(Op op, IrTy ty, std::vector<int> args, int dst = -1, u128 imm = 0) {
        Instr in;
        in.op = op;
        in.ty = ty;
        in.args = std::move(args);
        in.dst = dst;
        in.imm = imm;
        blk().code.push_back(std::move(in));
        return dst;
    }

    int emit_const(IrTy t, u128 v) {
        int r = new_reg(t);
        return emit(Op::Const, t, {}, r, v);
    }

    void terminate(Term t, int val = -1, int to = -1, int other = -1) {
        Block& b = blk();
        b.term = t;
        b.val = val;
        b.to = to;
        b.other = other;
    }

    void signature(Node& d, Function& f) {
        f.name = std::string(d.tok.text);
        f.nparams = d.arity;
        const Node& first = *d.kids[0];
        for (int i = 0; i < d.arity; ++i) {
            const Node& p = *first.kids[static_cast<size_t>(i)];
            IrTy t;
            if (!scalar(p.ty, t)) {
                err(p.tok, "not lowered yet: only numbers and bools can be parameters");
                t = IrTy{64, true, false};
            }
            f.vregs.push_back(t);
        }
        f.has_ret = scalar(d.ty, f.ret);
    }

    void def(Node& d, int index) {
        cur_ = index;
        vars_.clear();
        bb_ = new_block();
        for (auto& c : d.kids) {
            int next = new_block();
            for (int i = 0; i < d.arity; ++i) check_pattern(*c->kids[static_cast<size_t>(i)], i, next);
            int v = expr(*c->kids.back());
            if (fn().has_ret) {
                if (v < 0) unsupported(c->kids.back()->tok, "a body that gives no value");
                terminate(Term::Ret, v);
            } else {
                terminate(Term::Ret);
            }
            bb_ = next;
        }
        terminate(Term::Trap);
    }

    // jumps to `fail` when the pattern doesn't match, otherwise carries on in a new block
    void check_pattern(Node& p, int val, int fail) {
        IrTy t = reg_ty(val);
        if (p.kind == NodeKind::Name) {
            if (p.tok.text == "_") return;
            int d = new_reg(t);
            emit(Op::Copy, t, {val}, d);
            vars_[p.decl] = d;
            return;
        }
        u128 v = 0;
        if (p.kind == NodeKind::Int) parse_u128(p.tok.text, v);
        else if (p.kind == NodeKind::Bool) v = p.tok.text == "#t" ? 1 : 0;
        else unsupported(p.tok, "string patterns");
        int k = emit_const(t, v);
        int c = emit(Op::Eq, t, {val, k}, new_reg(IrTy{1, false, true}));
        int ok = new_block();
        terminate(Term::Br, c, ok, fail);
        bb_ = ok;
    }

    // gives back the register holding the value, or -1 when there is no value
    int expr(Node& n) {
        switch (n.kind) {
            case NodeKind::Int: {
                IrTy t;
                if (!scalar(n.ty, t)) unsupported(n.tok, "this number's type");
                u128 v = 0;
                parse_u128(n.tok.text, v);
                return emit_const(t, v);
            }
            case NodeKind::Bool:
                return emit_const(IrTy{1, false, true}, n.tok.text == "#t" ? 1 : 0);
            case NodeKind::Str:
                unsupported(n.tok, "strings outside of >>!");
            case NodeKind::Name: {
                bool value = n.sym == Sym::Val || n.sym == Sym::Mut || n.sym == Sym::Arg;
                if (!value) unsupported(n.tok, "using '" + std::string(n.tok.text) + "' as a value");
                auto g = globals_.find(n.decl);
                if (g != globals_.end()) return load_global(g->second);
                auto it = vars_.find(n.decl);
                if (it == vars_.end()) unsupported(n.tok, "a top-level value that failed to lower");
                return it->second;
            }
            case NodeKind::Bind:  return bind(n);
            case NodeKind::Monad: return monad(n);
            case NodeKind::Dyad:  return dyad(n);
            case NodeKind::Juxt:  return call(n);
            case NodeKind::Match: return match(n);
            case NodeKind::Block: {
                int last = -1;
                for (auto& k : n.kids) last = expr(*k);
                return last;
            }
            case NodeKind::Quiesce: {
                int c = expr(*n.kids[0]);
                if (c < 0) unsupported(n.tok, "quiesce with no value");
                terminate(Term::Exit, c);
                bb_ = new_block();  // anything after it is dead code, but it needs somewhere to go
                return -1;
            }
            default:
                unsupported(n.tok, "this kind of expression");
        }
    }

    int bind(Node& n) {
        Node& name = *n.kids[0];
        if (name.sym == Sym::Func) unsupported(n.tok, "binding a train");
        int r = expr(*n.kids[1]);
        if (r < 0) unsupported(n.kids[1]->tok, "binding something that has no value");
        IrTy t = reg_ty(r);
        if (captured_.count(name.decl)) return set_global(n, name, r, t);
        if (n.tok.kind == Tok::Assign) {
            auto it = vars_.find(name.decl);
            if (it == vars_.end()) unsupported(name.tok, "a function changing a top-level value");
            emit(Op::Copy, t, {r}, it->second);
            return it->second;
        }
        // always copy, so a name never shares a register with a mutable one
        int d = new_reg(t);
        emit(Op::Copy, t, {r}, d);
        vars_[name.decl] = d;
        return d;
    }

    int monad(Node& n) {
        if (!n.adverb.empty()) unsupported(n.tok, "the adverb '" + std::string(n.adverb) + "'");
        Node& arg = *n.kids[0];
        if (n.tok.kind == Tok::Print) {
            if (arg.kind == NodeKind::Str) {
                mod_.strings.push_back(decode(arg.tok.text));
                emit(Op::PrintStr, IrTy{}, {}, -1, mod_.strings.size() - 1);
                return -1;
            }
            int a = expr(arg);
            if (a < 0) unsupported(arg.tok, "printing something with no value");
            emit(Op::Print, reg_ty(a), {a});
            return -1;
        }
        if (n.tok.kind == Tok::Minus) {
            int a = expr(arg);
            if (a < 0) unsupported(arg.tok, "negating something with no value");
            IrTy t = reg_ty(a);
            return emit(Op::Neg, t, {a}, new_reg(t));
        }
        unsupported(n.tok, "the verb '" + std::string(n.tok.text) + "' with one argument");
    }

    int dyad(Node& n) {
        if (!n.adverb.empty()) unsupported(n.tok, "the adverb '" + std::string(n.adverb) + "'");
        Op op = Op::Add;
        bool cmp = false;
        switch (n.tok.kind) {
            case Tok::Plus:    op = Op::Add; break;
            case Tok::Minus:   op = Op::Sub; break;
            case Tok::Star:    op = Op::Mul; break;
            case Tok::Slash:   op = Op::Div; break;
            case Tok::Percent: op = Op::Rem; break;
            case Tok::Amp:     op = Op::And; break;
            case Tok::Shl:     op = Op::Shl; break;
            case Tok::Shr:     op = Op::Shr; break;
            case Tok::Lt:      op = Op::Lt; cmp = true; break;
            case Tok::Gt:      op = Op::Gt; cmp = true; break;
            case Tok::Eq:      op = Op::Eq; cmp = true; break;
            default:
                unsupported(n.tok, "the verb '" + std::string(n.tok.text) + "' with two arguments");
        }
        // right side first, same direction the language reads
        int b = expr(*n.kids[1]);
        int a = expr(*n.kids[0]);
        if (a < 0 || b < 0) unsupported(n.tok, "an operand with no value");
        IrTy t = reg_ty(a);
        return emit(op, t, {a, b}, new_reg(cmp ? IrTy{1, false, true} : t));
    }

    int call(Node& n) {
        if (n.shape != Shape::Call) unsupported(n.tok, "lists and strands");
        Node& head = *n.kids[0];
        if (head.kind != NodeKind::Name || head.sym != Sym::Func) unsupported(n.tok, "calling this");
        auto it = fn_index_.find(head.decl);
        if (it == fn_index_.end()) unsupported(head.tok, "calling a bound train or a nested function");
        int callee = it->second;

        std::vector<int> args(n.kids.size() - 1);
        for (size_t i = n.kids.size(); i-- > 1;) {
            int a = expr(*n.kids[i]);
            if (a < 0) unsupported(n.kids[i]->tok, "an argument with no value");
            args[i - 1] = a;
        }
        const Function& f = mod_.fns[static_cast<size_t>(callee)];
        IrTy rt = f.ret;
        int dst = f.has_ret ? new_reg(rt) : -1;
        return emit(Op::Call, rt, std::move(args), dst, callee);
    }

    int match(Node& n) {
        int s = expr(*n.kids[0]);
        if (s < 0) unsupported(n.kids[0]->tok, "matching on something with no value");
        int join = new_block();
        int res = -1;
        for (size_t i = 1; i < n.kids.size(); ++i) {
            Node& arm = *n.kids[i];
            if (arm.kids.size() != 2) unsupported(arm.tok, "matching several values at once");
            int next = new_block();
            check_pattern(*arm.kids[0], s, next);
            int v = expr(*arm.kids[1]);
            if (v >= 0) {
                if (res < 0) res = new_reg(reg_ty(v));
                emit(Op::Copy, reg_ty(res), {v}, res);
            }
            terminate(Term::Jmp, -1, join);
            bb_ = next;
        }
        terminate(Term::Trap);  // no arm matched
        bb_ = join;
        return res;
    }

    // top-level names that a function reads or writes. main's registers are not
    // visible to functions, so these have to live in memory
    void find_captured(std::vector<NodePtr>& program) {
        std::unordered_set<int> top;
        for (auto& n : program) {
            if (n->kind != NodeKind::Bind) continue;
            const Node& name = *n->kids[0];
            if (name.sym == Sym::Val || name.sym == Sym::Mut) top.insert(name.decl);
        }
        for (auto& n : program)
            if (n->kind == NodeKind::Def || n->kind == NodeKind::Isomorph) scan(*n, top);
    }

    void scan(const Node& n, const std::unordered_set<int>& top) {
        if (n.kind == NodeKind::Name && top.count(n.decl)) captured_.insert(n.decl);
        for (const auto& k : n.kids) scan(*k, top);
    }

    int load_global(int g) {
        IrTy t = mod_.globals[static_cast<size_t>(g)];
        int r = new_reg(t);
        return emit(Op::LoadG, t, {}, r, static_cast<u128>(g));
    }

    // the first <~ makes the cell, a later ~! writes to the same one
    int set_global(Node& n, Node& name, int r, IrTy t) {
        int g;
        auto it = globals_.find(name.decl);
        if (n.tok.kind == Tok::Assign) {
            if (it == globals_.end()) unsupported(name.tok, "a top-level value that failed to lower");
            g = it->second;
        } else {
            g = static_cast<int>(mod_.globals.size());
            mod_.globals.push_back(t);
            globals_[name.decl] = g;
        }
        emit(Op::StoreG, t, {r}, -1, static_cast<u128>(g));
        return r;
    }

    Diagnostics& diags_;
    Module mod_;
    std::unordered_map<int, int> fn_index_;  // declaration id of a def -> index in mod_.fns
    std::unordered_set<int> captured_;       // top-level names that a function uses
    std::unordered_map<int, int> globals_;   // declaration id -> index in mod_.globals
    std::unordered_map<int, int> vars_;      // declaration id -> register, per function
    int cur_ = 0;
    int bb_ = 0;
};

}  // namespace

Module lower(std::vector<NodePtr>& program, Diagnostics& diags) {
    return Lower(diags).run(program);
}

}  // namespace rill
