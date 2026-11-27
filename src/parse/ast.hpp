#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "lex/token.hpp"

namespace rill {

enum class NodeKind { Int, Str, Name, Bind, Monad, Dyad, Juxt, List, Verb, Train, Def, Clause, Block };

struct Node;
using NodePtr = std::unique_ptr<Node>;

struct Node {
    NodeKind kind;
    Token tok;                // literal, name, verb, or the bind operator
    std::string_view adverb;  // Monad/Dyad only, empty when there is none
    bool dots = false;        // List: written with ..
    bool has_end = false;     // List: last kid is the end of the range
    std::vector<NodePtr> kids;
};

std::string dump(const Node& n);

}  // namespace rill
