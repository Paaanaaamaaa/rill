#pragma once
#include <iosfwd>
#include <string>
#include <vector>
#include "common/source.hpp"

namespace rill {

struct Diag {
    int line, col, len;
    std::string msg;
};

class Diagnostics {
public:
    void error(int line, int col, int len, std::string msg);
    bool any() const { return !items_.empty(); }
    void print(const Source& src, std::ostream& os) const;

private:
    std::vector<Diag> items_;
};

}  
//rill namepsace