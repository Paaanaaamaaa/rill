#pragma once
#include <vector>
#include "common/diag.hpp"
#include "ir/ir.hpp"
#include "parse/ast.hpp"

namespace rill {

// run after infer, only on a tree with no errors
Module lower(std::vector<NodePtr>& program, Diagnostics& diags);

}  // namespace rill
