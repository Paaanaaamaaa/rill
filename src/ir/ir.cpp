#include "ir/ir.hpp"

namespace rill {
namespace {

std::string u128_str(u128 v) {
    if (v == 0) return "0";
    std::string s;
    while (v != 0) {
        s += static_cast<char>('0' + static_cast<int>(v % 10));
        v /= 10;
    }
    return std::string(s.rbegin(), s.rend());
}

const char* op_name(Op op) {
    switch (op) {
        case Op::Const:    return "const";
        case Op::Copy:     return "copy";
        case Op::Add:      return "add";
        case Op::Sub:      return "sub";
        case Op::Mul:      return "mul";
        case Op::Div:      return "div";
        case Op::Rem:      return "rem";
        case Op::And:      return "and";
        case Op::Shl:      return "shl";
        case Op::Shr:      return "shr";
        case Op::Neg:      return "neg";
        case Op::Lt:       return "lt";
        case Op::Gt:       return "gt";
        case Op::Eq:       return "eq";
        case Op::Call:     return "call";
        case Op::Print:    return "print";
        case Op::PrintStr: return "printstr";
        case Op::LoadG: return "loadg";
        case Op::StoreG: return "storeg";
    }
    return "?";
}

std::string vr(int v) { return "v" + std::to_string(v); }
std::string bb(int b) { return "b" + std::to_string(b); }

std::string args_str(const std::vector<int>& a) {
    std::string s;
    for (size_t i = 0; i < a.size(); ++i) {
        if (i) s += ", ";
        s += vr(a[i]);
    }
    return s;
}

std::string quote(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '\n') o += "\\n";
        else if (c == '\t') o += "\\t";
        else if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\0') o += "\\0";
        else o += c;
    }
    return o + "\"";
}

}  // namespace

std::string ty_str(const IrTy& t) {
    if (t.is_bool) return "bool";
    return std::string(t.sign ? "i" : "u") + std::to_string(t.width);
}

std::string ir_dump(const Module& m) {
    std::string out;
    for (size_t gi = 0; gi < m.globals.size(); ++gi)
        out += "global g" + std::to_string(gi) + ": " + ty_str(m.globals[gi]) + "\n";
    if (!m.globals.empty()) out += "\n";
    for (size_t fi = 0; fi < m.fns.size(); ++fi) {
        const Function& f = m.fns[fi];
        if (fi) out += "\n";
        out += "fn " + f.name + "(";
        for (int i = 0; i < f.nparams; ++i) {
            if (i) out += ", ";
            out += vr(i) + ": " + ty_str(f.vregs[static_cast<size_t>(i)]);
        }
        out += ")";
        if (f.has_ret) out += " -> " + ty_str(f.ret);
        out += "\n";

        for (size_t b = 0; b < f.blocks.size(); ++b) {
            const Block& bl = f.blocks[b];
            out += bb(static_cast<int>(b)) + ":\n";
            for (const Instr& in : bl.code) {
                out += "  ";
                if (in.dst >= 0) out += vr(in.dst) + " = ";
                out += op_name(in.op);
                if (in.op == Op::Const) {
                    out += "." + ty_str(in.ty) + " ";
                    if (in.ty.is_bool) out += in.imm ? "#t" : "#f";
                    else out += u128_str(in.imm);
                } else if (in.op == Op::LoadG || in.op == Op::StoreG) {
                    out += "." + ty_str(in.ty) + " g" + u128_str(in.imm);
                    if (!in.args.empty()) out += ", " + args_str(in.args);
                } else if (in.op == Op::PrintStr) {
                    out += " s" + u128_str(in.imm);
                } else if (in.op == Op::Call) {
                    out += " " + m.fns[static_cast<size_t>(in.imm)].name + "(" + args_str(in.args) + ")";
                } else {
                    out += "." + ty_str(in.ty) + " " + args_str(in.args);
                }
                out += "\n";
            }
            switch (bl.term) {
                case Term::Jmp:  out += "  jmp " + bb(bl.to) + "\n"; break;
                case Term::Br:
                    out += "  br " + vr(bl.val) + ", " + bb(bl.to) + ", " + bb(bl.other) + "\n";
                    break;
                case Term::Ret:
                    out += "  ret";
                    if (bl.val >= 0) out += " " + vr(bl.val);
                    out += "\n";
                    break;
                case Term::Exit: out += "  exit " + vr(bl.val) + "\n"; break;
                case Term::Trap: out += "  trap\n"; break;
                case Term::None: out += "  <no terminator>\n"; break;
            }
        }
    }
    if (!m.strings.empty()) out += "\n";
    for (size_t i = 0; i < m.strings.size(); ++i)
        out += "str s" + std::to_string(i) + " = " + quote(m.strings[i]) + "\n";
    return out;
}

}  // namespace rill
