#include "parse/ast.hpp"

namespace rill {
namespace {

const char* sym_name(Sym s) {
    switch (s) {
        case Sym::Func:   return "fn";
        case Sym::Val:    return "val";
        case Sym::Mut:    return "mut";
        case Sym::Arg:    return "arg";
        case Sym::Stream: return "stream";
        case Sym::Region: return "region";
        case Sym::Type:   return "type";
        case Sym::None:   break;
    }
    return "";
}

}  // namespace

std::string dump(const Node& n, bool resolved) {
    std::string self(n.tok.text);
    auto all = [&](size_t from = 0) {
        std::string s;
        for (size_t i = from; i < n.kids.size(); ++i) s += " " + dump(*n.kids[i], resolved);
        return s;
    };
    // clause and arm look the same: (name (patterns) body)
    auto pats_body = [&](const char* name) {
        std::string s = std::string("(") + name + " (";
        for (size_t i = 0; i + 1 < n.kids.size(); ++i) {
            if (i) s += " ";
            s += dump(*n.kids[i], resolved);
        }
        return s + ") " + dump(*n.kids.back(), resolved) + ")";
    };
    switch (n.kind) {
        case NodeKind::Int:
        case NodeKind::Str:
            return self;
        case NodeKind::Name:
            if (resolved && n.sym != Sym::None) return self + ":" + sym_name(n.sym);
            return self;
        case NodeKind::Verb:
            return self + std::string(n.adverb);
        case NodeKind::List: {
            std::string s = "(list";
            size_t items = n.kids.size() - (n.has_end ? 1 : 0);
            for (size_t i = 0; i < items; ++i) s += " " + dump(*n.kids[i], resolved);
            if (n.dots) s += " ..";
            if (n.has_end) s += " " + dump(*n.kids.back(), resolved);
            return s + ")";
        }
        case NodeKind::Juxt:
            if (resolved) {
                switch (n.shape) {
                    case Shape::List:    return "(strand" + all() + ")";
                    case Shape::Call:    return "(call" + all() + ")";
                    case Shape::Unknown: return "(apply?" + all() + ")";
                    case Shape::CallStrand:
                        return "(call " + dump(*n.kids[0], resolved) + " (strand" + all(1) + "))";
                    case Shape::None:    break;
                }
            }
            return "(juxt" + all() + ")";
        case NodeKind::Def:      return "(def " + self + all() + ")";
        case NodeKind::Isomorph: return "(isomorph " + self + all() + ")";
        case NodeKind::Orbit:    return "(orbit " + self + all() + ")";
        case NodeKind::Lattice:  return "(lattice " + self + all() + ")";
        case NodeKind::Field:    return "(" + self + all() + ")";
        case NodeKind::Clause:   return pats_body("clause");
        case NodeKind::Arm:      return pats_body("arm");
        case NodeKind::Block:    return "(block" + all() + ")";
        case NodeKind::Match:    return "(?|" + all() + ")";
        case NodeKind::Fixpoint: return "(fixpoint" + all() + ")";
        case NodeKind::Quiesce:  return "(quiesce" + all() + ")";
        case NodeKind::Manifold: return "(manifold" + all() + ")";
        case NodeKind::Train:    return "(train" + all() + ")";
        default:
            break;
    }
    return "(" + self + std::string(n.adverb) + all() + ")";
}

}  // namespace rill
