#pragma once
#include <string>
#include <vector>
#include "common/diag.hpp"
#include "parse/ast.hpp"

namespace rill {

// run after sema, only on a tree with no errors. gives back "name : type" lines
std::vector<std::string> infer(std::vector<NodePtr>& program, Diagnostics& diags);

}  // namespace rill
