#include "parse/ast.hpp"

namespace rill {

std::string dump(const Node& n) {
    std::string self(n.tok.text);
    auto all = [&]() {
        std::string s;
        for (const auto& k : n.kids) s += " " + dump(*k);
        return s;
    };
    // clause and arm look the same: (name (patterns) body)
    auto pats_body = [&](const char* name) {
        std::string s = std::string("(") + name + " (";
        for (size_t i = 0; i + 1 < n.kids.size(); ++i) {
            if (i) s += " ";
            s += dump(*n.kids[i]);
        }
        return s + ") " + dump(*n.kids.back()) + ")";
    };
    switch (n.kind) {
        case NodeKind::Int:
        case NodeKind::Str:
        case NodeKind::Name:
            return self;
        case NodeKind::Verb:
            return self + std::string(n.adverb);
        case NodeKind::List: {
            std::string s = "(list";
            size_t items = n.kids.size() - (n.has_end ? 1 : 0);
            for (size_t i = 0; i < items; ++i) s += " " + dump(*n.kids[i]);
            if (n.dots) s += " ..";
            if (n.has_end) s += " " + dump(*n.kids.back());
            return s + ")";
        }
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
        default:
            break;
    }
    std::string s = "(";
    if (n.kind == NodeKind::Juxt) s += "juxt";
    else if (n.kind == NodeKind::Train) s += "train";
    else s += self + std::string(n.adverb);
    return s + all() + ")";
}

}  // namespace rill
