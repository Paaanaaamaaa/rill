#include "sema/types.hpp"

namespace rill {

TyName parse_ty_name(std::string_view s) {
    TyName r;
    if (s == "bool") { r.kind = TyKind::Bool; return r; }
    if (s == "str") { r.kind = TyKind::Str; return r; }
    if (s.size() < 2 || (s[0] != 'i' && s[0] != 'u')) return r;
    int w = 0;
    for (size_t i = 1; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return r;
        if (w < 100000) w = w * 10 + (s[i] - '0');
    }
    r.kind = TyKind::Int;
    r.sign = s[0] == 'i';
    r.width = w;
    r.bad_width = w < 1 || w > 128 || s[1] == '0';
    return r;
}

}  // namespace rill
