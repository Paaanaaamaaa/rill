#pragma once
#include <vector>
#include "common/diag.hpp"
#include "parse/ast.hpp"

namespace rill {

void analyze(std::vector<NodePtr>& program, Diagnostics& diags);

}  // namespace rill
