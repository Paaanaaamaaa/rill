#include <iostream>
#include <string>
#include "common/diag.hpp"
#include "common/source.hpp"
#include "lex/layout.hpp"
#include "lex/lexer.hpp"
#include "parse/parser.hpp"
#include "sema/sema.hpp"

int main(int argc, char** argv) {
    std::string mode = argc == 3 ? argv[1] : "";
    if (mode != "--tokens" && mode != "--ast" && mode != "--sema") {
        std::cerr << "usage: rill --tokens|--ast|--sema <file.rl>\n";
        return 2;
    }
    rill::Source src;
    if (!rill::Source::load(argv[2], src)) {
        std::cerr << "rill: cannot read " << argv[2] << '\n';
        return 1;
    }
    rill::Diagnostics diags;
    auto toks = rill::apply_layout(rill::Lexer(src, diags).run(), diags);

    if (mode == "--tokens") {
        for (const auto& t : toks)
            std::cout << t.line << ':' << t.col << "\t" << rill::tok_name(t.kind)
                      << "\t" << (t.kind == rill::Tok::Newline ? "\\n" : std::string(t.text)) << '\n';
        diags.print(src, std::cerr);
        return diags.any() ? 1 : 0;
    }

    // no point parsing a file the lexer already rejected
    if (diags.any()) {
        diags.print(src, std::cerr);
        return 1;
    }
    auto tree = rill::Parser(toks, diags).parse();
    // a tree with parse errors in it would only cause fake sema errors
    bool sema = mode == "--sema";
    if (sema && !diags.any()) rill::analyze(tree, diags);
    for (const auto& n : tree) std::cout << rill::dump(*n, sema) << '\n';
    diags.print(src, std::cerr);
    return diags.any() ? 1 : 0;
}
