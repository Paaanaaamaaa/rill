#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "backend/codegen.hpp"
#include "common/diag.hpp"
#include "common/source.hpp"
#include "ir/lower.hpp"
#include "lex/layout.hpp"
#include "lex/lexer.hpp"
#include "parse/parser.hpp"
#include "sema/infer.hpp"
#include "sema/sema.hpp"

namespace {

std::string sh_quote(const std::string& s) {
    std::string o = "'";
    for (char c : s) {
        if (c == '\'') o += "'\\''";
        else o += c;
    }
    return o + "'";
}

int usage() {
    std::cerr << "usage: rill <file.rl> [-o out]\n"
                 "       rill --tokens|--ast|--sema|--types|--ir|--asm <file.rl>\n";
    return 2;
}

// temporary: as and ld do the last two steps until we have our own encoder
int assemble(const std::string& text, const std::string& out) {
    std::string s = out + ".s", o = out + ".o";
    {
        std::ofstream f(s, std::ios::binary);
        if (!f) {
            std::cerr << "rill: cannot write " << s << '\n';
            return 1;
        }
        f << text;
    }
    std::string cmd = "as -o " + sh_quote(o) + " " + sh_quote(s) + " && ld -o " + sh_quote(out) + " " + sh_quote(o);
    int rc = std::system(cmd.c_str());
    std::remove(s.c_str());
    std::remove(o.c_str());
    if (rc != 0) {
        std::cerr << "rill: as or ld failed, is binutils installed?\n";
        return 1;
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> a(argv + 1, argv + argc);
    std::string mode, path, out = "a.out";
    if (a.size() == 2 && a[0].rfind("--", 0) == 0) {
        mode = a[0];
        path = a[1];
    } else if (a.size() == 1 && !a[0].empty() && a[0][0] != '-') {
        mode = "--build";
        path = a[0];
    } else if (a.size() == 3 && a[1] == "-o") {
        mode = "--build";
        path = a[0];
        out = a[2];
    } else {
        return usage();
    }
    bool known = mode == "--tokens" || mode == "--ast" || mode == "--sema" || mode == "--types" ||
                 mode == "--ir" || mode == "--asm" || mode == "--build";
    if (!known) return usage();

    rill::Source src;
    if (!rill::Source::load(path, src)) {
        std::cerr << "rill: cannot read " << path << '\n';
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

    if (mode == "--ast" || mode == "--sema") {
        // a tree with parse errors in it would only cause fake sema errors
        if (mode == "--sema" && !diags.any()) rill::analyze(tree, diags);
        for (const auto& n : tree) std::cout << rill::dump(*n, mode == "--sema") << '\n';
        diags.print(src, std::cerr);
        return diags.any() ? 1 : 0;
    }

    if (!diags.any()) rill::analyze(tree, diags);
    std::vector<std::string> lines;
    if (!diags.any()) lines = rill::infer(tree, diags);

    if (mode == "--types") {
        if (!diags.any())
            for (const auto& l : lines) std::cout << l << '\n';
        diags.print(src, std::cerr);
        return diags.any() ? 1 : 0;
    }

    if (!diags.any()) {
        rill::Module m = rill::lower(tree, diags);
        if (!diags.any()) {
            if (mode == "--ir") {
                std::cout << rill::ir_dump(m);
            } else {
                std::string text, error;
                if (!rill::codegen_x86(m, text, error)) {
                    std::cerr << "rill: " << error << '\n';
                    return 1;
                }
                if (mode == "--asm") std::cout << text;
                else return assemble(text, out);
            }
        }
    }
    diags.print(src, std::cerr);
    return diags.any() ? 1 : 0;
}
