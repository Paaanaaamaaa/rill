#pragma once
#include <string>
#include "ir/ir.hpp"

namespace rill {

// gives GNU as text (intel syntax). false with a message when the module has something
// the backend can't do yet
bool codegen_x86(const Module& m, std::string& asm_text, std::string& error);

}  // namespace rill
