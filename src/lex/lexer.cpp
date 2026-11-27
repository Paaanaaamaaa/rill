#include "lex/lexer.hpp"
#include <algorithm>
#include <cctype>

namespace rill {
namespace {

struct OpEntry { std::string_view text; Tok kind; };

const std::vector<OpEntry>& op_table() {
    static const std::vector<OpEntry> table = [] {
        std::vector<OpEntry> t = {
#define OP(name, text) {text, Tok::name},
#include "lex/operators.def"
#undef OP
        };
        std::stable_sort(t.begin(), t.end(),
                         [](const OpEntry& a, const OpEntry& b) { return a.text.size() > b.text.size(); });
        return t;
    }();
    return table;
}

Tok keyword_or_ident(std::string_view w) {
#define KW(name, text) if (w == text) return Tok::Kw##name;
#include "lex/keywords.def"
#undef KW
    return Tok::Ident;
}

bool is_digit(char c) { return std::isdigit(static_cast<unsigned char>(c)); }
bool is_word_start(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool is_word_char(char c) { return is_word_start(c) || is_digit(c); }

}  

char Lexer::peek(size_t ahead) const {
    size_t i = pos_ + ahead;
    return i < src_.text.size() ? src_.text[i] : '\0';
}

char Lexer::advance() {
    char c = src_.text[pos_++];
    if (c == '\n') { ++line_; col_ = 1; } else { ++col_; }
    return c;
}

Token Lexer::make(Tok kind, size_t start, int line, int col) const {
    return {kind, std::string_view(src_.text).substr(start, pos_ - start), line, col};
}

// 42, 5'67 (i67 literal), 5'u67 (u67 literal). Whether the width is in 1..128
// is checked later in sema,
Token Lexer::number() {
    size_t start = pos_;
    int l = line_, c = col_;
    while (is_digit(peek())) advance();
    if (peek() == '\'') {
        advance();
        if (peek() == 'i' || peek() == 'u') advance();
        if (!is_digit(peek()))
            diags_.error(l, c, static_cast<int>(pos_ - start), "expected a bit width after '");
        while (is_digit(peek())) advance();
    }
    return make(Tok::IntLit, start, l, c);
}

Token Lexer::word() {
    size_t start = pos_;
    int l = line_, c = col_;
    while (is_word_char(peek())) advance();
    Token t = make(Tok::Ident, start, l, c);
    t.kind = keyword_or_ident(t.text);
    return t;
}

bool Lexer::operator_at(Token& out) {
    std::string_view rest = std::string_view(src_.text).substr(pos_);
    for (const OpEntry& op : op_table()) {
        if (rest.starts_with(op.text)) {
            size_t start = pos_;
            int l = line_, c = col_;
            for (size_t i = 0; i < op.text.size(); ++i) advance();
            out = make(op.kind, start, l, c);
            return true;
        }
    }
    return false;
}

std::vector<Token> Lexer::run() {
    std::vector<Token> out;
    while (pos_ < src_.text.size()) {
        char c = peek();
        if (c == ' ' || c == '\r') { advance(); continue; }
        if (c == '\t') {
            diags_.error(line_, col_, 1, "tabs are not allowed, indent with spaces");
            advance();
            continue;
        }
        if (c == '%' && peek(1) == '%') {  // comment wins over two '%' operators
            while (pos_ < src_.text.size() && peek() != '\n') advance();
            continue;
        }
        if (c == '\n') {
            size_t start = pos_; int l = line_, col = col_;
            advance();
            out.push_back(make(Tok::Newline, start, l, col));
            continue;
        }
        if (is_digit(c)) { out.push_back(number()); continue; }
        if (is_word_start(c)) { out.push_back(word()); continue; }
        Token t;
        if (operator_at(t)) { out.push_back(t); continue; }
        diags_.error(line_, col_, 1, std::string("unexpected character '") + c + "'");
        advance();
    }
    out.push_back({Tok::Eof, {}, line_, col_});
    return out;
}

}  // rill namespace.