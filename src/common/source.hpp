#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace rill {

struct Source {
    std::string path;
    std::string text;
    // offset of each line's first char
    std::vector<size_t> line_starts;  

    static bool load(const std::string& path, Source& out);
    // 1 based, without the newline
    std::string_view line(int n) const;  
};

}  
// rill namespace.