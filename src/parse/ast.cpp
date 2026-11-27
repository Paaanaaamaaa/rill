#include "parse/ast.hpp"

namespace rill {

std::string dump(const Node& n) {
    std::string self(n.tok.text);
    switch (n.kind) {
        case NodeKind::Int:
        case NodeKind::Str:
        case NodeKind::Name:
            return self;
        case NodeKind::Def: {
            std::string s = "(def " + self;
            for (const auto& k : n.kids) s += " " + dump(*k);
            return s + ")";
        }
        case NodeKind::Clause: {
            std::string s = "(clause (";
            for (size_t i = 0; i + 1 < n.kids.size(); ++i) {
                if (i) s += " ";
                s += dump(*n.kids[i]);
            }
            return s + ") " + dump(*n.kids.back()) + ")";
        }
        case NodeKind::Block: {
            std::string s = "(block";
            for (const auto& k : n.kids) s += " " + dump(*k);
            return s + ")";
        }
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
        default:
            break;
    }
    std::string s = "(";
    if (n.kind == NodeKind::Juxt) s += "juxt";
    else if (n.kind == NodeKind::Train) s += "train";
    else s += self + std::string(n.adverb);
    for (const auto& k : n.kids) s += " " + dump(*k);
    return s + ")";
}

}  // namespace rill
