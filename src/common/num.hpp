#pragma once
#include <string_view>

namespace rill {

__extension__ typedef unsigned __int128 u128;

// digits only, false when the value doesn't fit in 128 bits
inline bool parse_u128(std::string_view digits, u128& out) {
    out = 0;
    const u128 max = ~static_cast<u128>(0);
    for (char c : digits) {
        unsigned d = static_cast<unsigned>(c - '0');
        if (out > (max - d) / 10) return false;
        out = out * 10 + d;
    }
    return true;
}

}  // namespace rill
