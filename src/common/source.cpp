#include "common/source.hpp"
#include <fstream>
#include <sstream>

namespace rill {

bool Source::load(const std::string& path, Source& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out.path = path;
    out.text = ss.str();
    out.line_starts = {0};
    for (size_t i = 0; i < out.text.size(); ++i)
        if (out.text[i] == '\n') out.line_starts.push_back(i + 1);
    return true;
}

std::string_view Source::line(int n) const {
    if (n < 1 || static_cast<size_t>(n) > line_starts.size()) return {};
    size_t b = line_starts[n - 1];
    size_t e = static_cast<size_t>(n) < line_starts.size() ? line_starts[n] - 1 : text.size();
    return std::string_view(text).substr(b, e - b);
}

}  
//rill namespace.