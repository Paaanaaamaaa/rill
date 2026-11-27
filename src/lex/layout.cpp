#include "lex/layout.hpp"

namespace rill {

std::vector<Token> apply_layout(const std::vector<Token>& in, Diagnostics& diags) {
    std::vector<Token> out;
    std::vector<int> levels = {0};  // indent widths of the blocks we are inside
    int depth = 0;                  // how many ( [ { are open right now
    bool line_start = true;

    for (const Token& t : in) {
        if (t.kind == Tok::Eof) {
            // file might not end with a newline, add one so the parser sees a full line
            if (!out.empty() && out.back().kind != Tok::Newline)
                out.push_back({Tok::Newline, {}, t.line, t.col});
            while (levels.size() > 1) {
                levels.pop_back();
                out.push_back({Tok::Dedent, {}, t.line, t.col});
            }
            out.push_back(t);
            break;
        }

        if (t.kind == Tok::Newline) {
            // a newline with nothing before it is an empty line, drop it
            // inside brackets the line just keeps going, drop it too
            if (line_start || depth > 0) continue;
            out.push_back(t);
            line_start = true;
            continue;
        }

        if (line_start && depth == 0) {
            int indent = t.col - 1;  // tabs are already an error, so this is spaces only
            if (indent > levels.back()) {
                levels.push_back(indent);
                out.push_back({Tok::Indent, {}, t.line, t.col});
            } else {
                while (indent < levels.back()) {
                    levels.pop_back();
                    out.push_back({Tok::Dedent, {}, t.line, t.col});
                }
                // went past the level we wanted, so it was never a level
                // TODO: better recovery, right now the next line just errors again
                if (indent != levels.back())
                    diags.error(t.line, 1, t.col, "indentation does not match any outer block");
            }
        }
        line_start = false;

        if (t.kind == Tok::LParen || t.kind == Tok::LBracket || t.kind == Tok::LBrace) depth++;
        if ((t.kind == Tok::RParen || t.kind == Tok::RBracket || t.kind == Tok::RBrace) && depth > 0) depth--;

        out.push_back(t);
    }
    return out;
}

}  // namespace rill
