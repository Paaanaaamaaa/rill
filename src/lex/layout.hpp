#pragma once
#include <vector>
#include "common/diag.hpp"
#include "lex/token.hpp"

namespace rill {

std::vector<Token> apply_layout(const std::vector<Token>& in, Diagnostics& diags);

}  // namespace rill
