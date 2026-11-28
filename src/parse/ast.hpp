#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "lex/token.hpp"

namespace rill {

enum class NodeKind {
    Int, Str, Bool, Name, Bind, Monad, Dyad, Juxt, List, Verb, Train,
    Def, Clause, Block, Match, Arm,
    Orbit, Isomorph, Fixpoint, Quiesce, Manifold, Lattice, Field,
    Sig
};

// what a name turned out to be, filled in by sema
enum class Sym { None, Func, Val, Mut, Arg, Stream, Region, Type };

// what a juxt turned out to be, filled in by sema
enum class Shape { None, List, Call, CallStrand, Unknown };

struct Node;
using NodePtr = std::unique_ptr<Node>;

struct Node {
    NodeKind kind;
    Token tok;                // literal, name, verb, or the bind operator
    std::string_view adverb;  // Monad/Dyad/Verb only, empty when there is none
    bool dots = false;        // List: written with ..
    bool has_end = false;     // List: last kid is the end of the range
    Sym sym = Sym::None;      // Name
    int arity = -1;           // Name/Def: -1 means any
    Shape shape = Shape::None;  // Juxt
    int decl = -1;            // which declaration this points at, set by sema
    const Node* sig = nullptr;  // Def/Isomorph: its signature line, if any
    bool annotated = false;   // Bind: written as  x : type <~ ...
    int tyform = 0;   // Name in a signature: 0 plain, 1 [t], 2 [t..]
    int annform = 0;  // Bind: the same thing for its annotation
    Token ann{};              // Bind: the type name
    std::vector<NodePtr> kids;
};

// resolved = print what sema found (name kinds, calls vs strands)
std::string dump(const Node& n, bool resolved = false);

}  // namespace rill
