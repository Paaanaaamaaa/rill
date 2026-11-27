#include "common/diag.hpp"
#include <ostream>

namespace rill {

void Diagnostics::error(int line, int col, int len, std::string msg) {
    items_.push_back({line, col, len < 1 ? 1 : len, std::move(msg)});
}

void Diagnostics::print(const Source& src, std::ostream& os) const {
    for (const Diag& d : items_) {
        os << src.path << ':' << d.line << ':' << d.col << ": error: " << d.msg << '\n';
        os << "  " << src.line(d.line) << '\n';
        os << "  " << std::string(d.col - 1, ' ') << std::string(d.len, '^') << '\n';
    }
}

}  
//rill namespace.