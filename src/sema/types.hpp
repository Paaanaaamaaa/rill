#pragma once
#include <string_view>

namespace rill {

enum class TyKind { NotType, Int, Bool, Str };

struct TyName {
    TyKind kind = TyKind::NotType;
    bool sign = true;
    int width = 0;
    bool bad_width = false;  // looks like i0 or u999, reserved but not usable
};

TyName parse_ty_name(std::string_view s);

}  // namespace rill
