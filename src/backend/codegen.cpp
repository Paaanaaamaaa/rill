#include "backend/codegen.hpp"
#include <string>

namespace rill {
namespace {

const char* RUNTIME = R"(
rt_print_u:
    sub rsp, 32
    lea rsi, [rsp + 31]
    mov byte ptr [rsi], 10
    mov rax, rdi
    mov ecx, 10
.Lrt_u_loop:
    xor edx, edx
    div rcx
    add dl, 48
    dec rsi
    mov byte ptr [rsi], dl
    test rax, rax
    jnz .Lrt_u_loop
    lea rdx, [rsp + 32]
    sub rdx, rsi
    mov edi, 1
    mov eax, 1
    syscall
    add rsp, 32
    ret

rt_print_i:
    test rdi, rdi
    jns rt_print_u
    push rdi
    lea rsi, [rip + rt_minus]
    mov edx, 1
    mov edi, 1
    mov eax, 1
    syscall
    pop rdi
    neg rdi
    jmp rt_print_u

rt_print_b:
    lea rsi, [rip + rt_bool_txt]
    test rdi, rdi
    jz .Lrt_b_go
    add rsi, 3
.Lrt_b_go:
    mov edx, 3
    mov edi, 1
    mov eax, 1
    syscall
    ret

rt_trap:
    mov edi, 2
    lea rsi, [rip + rt_trap_msg]
    mov edx, 25
    mov eax, 1
    syscall
    mov edi, 1
    mov eax, 60
    syscall
)";

const char* RODATA = R"(
.section .rodata
rt_minus: .ascii "-"
rt_bool_txt: .ascii "#f\n#t\n"
rt_trap_msg: .ascii "rill: no pattern matched\n"
)";

class Gen {
public:
    explicit Gen(const Module& m) : m_(m) {}

    bool run(std::string& out, std::string& error) {
        for (const Function& f : m_.fns) {
            for (const IrTy& t : f.vregs) {
                if (!t.is_bool && t.width > 64) {
                    error = "'" + f.name + "' uses " + ty_str(t) +
                            ", widths above 64 need the wide-int pass, which isn't written yet";
                    return false;
                }
            }
        }
        out_ += ".intel_syntax noprefix\n.text\n.globl _start\n_start:\n    call rl_main\n    ud2\n";
        for (size_t i = 0; i < m_.fns.size(); ++i) function(static_cast<int>(i));
        out_ += RUNTIME;
        out_ += RODATA;
        for (size_t i = 0; i < m_.strings.size(); ++i) {
            out_ += ".Lstr" + std::to_string(i) + ": .byte ";
            for (unsigned char c : m_.strings[i]) out_ += std::to_string(static_cast<int>(c)) + ", ";
            out_ += "10\n";  // print puts a newline after every value
        }
        out_ += ".section .note.GNU-stack,\"\",@progbits\n";
        out = std::move(out_);
        return true;
    }

private:
    void line(const std::string& s) { out_ += "    " + s + "\n"; }

    static std::string slot(int v) { return "qword ptr [rbp - " + std::to_string(8 * (v + 1)) + "]"; }
    void load(const char* reg, int v) { line(std::string("mov ") + reg + ", " + slot(v)); }
    void store(int v) { line("mov " + slot(v) + ", rax"); }

    static std::string fn_label(int fi) { return fi == 0 ? "rl_main" : "rl_f_" + std::to_string(fi); }
    std::string block_label(int b) const { return ".Lf" + std::to_string(fi_) + "_b" + std::to_string(b); }

    // keep the value in rax the way the slot expects it
    void normalize(const IrTy& t) {
        if (t.is_bool) { line("and rax, 1"); return; }
        if (t.width >= 64) return;
        std::string sh = std::to_string(64 - t.width);
        line("shl rax, " + sh);
        line(std::string(t.sign ? "sar" : "shr") + " rax, " + sh);
    }

    void function(int fi) {
        const Function& f = m_.fns[static_cast<size_t>(fi)];
        fi_ = fi;
        out_ += "\n" + fn_label(fi) + ":  # " + f.name + "\n";
        size_t frame = (f.vregs.size() * 8 + 15) / 16 * 16;
        line("push rbp");
        line("mov rbp, rsp");
        line("sub rsp, " + std::to_string(frame));
        for (int i = 0; i < f.nparams; ++i) {
            line("mov rax, qword ptr [rbp + " + std::to_string(16 + 8 * i) + "]");
            store(i);
        }
        for (size_t b = 0; b < f.blocks.size(); ++b) {
            out_ += block_label(static_cast<int>(b)) + ":\n";
            for (const Instr& in : f.blocks[b].code) instr(in);
            terminator(f.blocks[b]);
        }
    }

    void instr(const Instr& in) {
        const IrTy& t = in.ty;
        switch (in.op) {
            case Op::Const:
                line("movabs rax, " + std::to_string(static_cast<unsigned long long>(in.imm)));
                store(in.dst);
                return;
            case Op::Copy:
                load("rax", in.args[0]);
                store(in.dst);
                return;
            case Op::Neg:
                load("rax", in.args[0]);
                line("neg rax");
                normalize(t);
                store(in.dst);
                return;
            case Op::Print:
                load("rdi", in.args[0]);
                line(t.is_bool ? "call rt_print_b" : t.sign ? "call rt_print_i" : "call rt_print_u");
                return;
            case Op::PrintStr:
                line("lea rsi, [rip + .Lstr" + std::to_string(static_cast<size_t>(in.imm)) + "]");
                line("mov edx, " + std::to_string(m_.strings[static_cast<size_t>(in.imm)].size() + 1));
                line("mov edi, 1");
                line("mov eax, 1");
                line("syscall");
                return;
            case Op::Call:
                for (size_t i = in.args.size(); i-- > 0;) line("push " + slot(in.args[i]));
                line("call " + fn_label(static_cast<int>(in.imm)));
                if (!in.args.empty()) line("add rsp, " + std::to_string(8 * in.args.size()));
                if (in.dst >= 0) store(in.dst);
                return;
            default:
                break;
        }

        // everything left takes two operands
        load("rax", in.args[0]);
        load("rcx", in.args[1]);
        switch (in.op) {
            case Op::Add: line("add rax, rcx"); break;
            case Op::Sub: line("sub rax, rcx"); break;
            case Op::Mul: line("imul rax, rcx"); break;
            case Op::And: line("and rax, rcx"); break;
            case Op::Shl: line("shl rax, cl"); break;
            case Op::Shr: line(t.sign ? "sar rax, cl" : "shr rax, cl"); break;
            case Op::Div:
            case Op::Rem:
                if (t.sign) {
                    line("cqo");
                    line("idiv rcx");
                } else {
                    line("xor edx, edx");
                    line("div rcx");
                }
                if (in.op == Op::Rem) line("mov rax, rdx");
                break;
            case Op::Lt:
            case Op::Gt:
            case Op::Eq: {
                const char* cc = in.op == Op::Eq ? "e"
                               : in.op == Op::Lt ? (t.sign ? "l" : "b")
                                                 : (t.sign ? "g" : "a");
                line("cmp rax, rcx");
                line(std::string("set") + cc + " al");
                line("movzx eax, al");
                store(in.dst);
                return;
            }
            default:
                break;
        }
        normalize(t);
        store(in.dst);
    }

    void terminator(const Block& b) {
        switch (b.term) {
            case Term::Jmp:
                line("jmp " + block_label(b.to));
                break;
            case Term::Br:
                load("rax", b.val);
                line("test rax, rax");
                line("jnz " + block_label(b.to));
                line("jmp " + block_label(b.other));
                break;
            case Term::Ret:
                if (b.val >= 0) load("rax", b.val);
                line("leave");
                line("ret");
                break;
            case Term::Exit:
                load("rdi", b.val);
                line("mov eax, 60");
                line("syscall");
                break;
            case Term::Trap:
                line("jmp rt_trap");
                break;
            case Term::None:
                line("ud2");  // lowering never leaves a block open, this is just in case
                break;
        }
    }

    const Module& m_;
    std::string out_;
    int fi_ = 0;
};

}  // namespace

bool codegen_x86(const Module& m, std::string& asm_text, std::string& error) {
    return Gen(m).run(asm_text, error);
}

}  // namespace rill
