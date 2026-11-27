#include <iostream>
#include <string>
#include "common/diag.hpp"
#include "common/source.hpp"
#include "lex/lexer.hpp"
#include "lex/layout.hpp"

int main(int argc, char** argv) {
    if (argc != 3 || std::string(argv[1]) != "--tokens") {
        std::cerr << "usage: rill --tokens <file.rl>\n";
        return 2;
    }
    rill::Source src;
    if (!rill::Source::load(argv[2], src)) {
        std::cerr << "rill: cannot read " << argv[2] << '\n';
        return 1;
    }
    rill::Diagnostics diags;
    auto toks = rill::apply_layout(rill::Lexer(src, diags).run(), diags);
    for (const auto& t : toks)
        std::cout << t.line << ':' << t.col << "\t" << rill::tok_name(t.kind)
                  << "\t" << (t.kind == rill::Tok::Newline ? "\\n" : std::string(t.text)) << '\n';
    diags.print(src, std::cerr);
    return diags.any() ? 1 : 0;
}