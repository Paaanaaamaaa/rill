#include "backend/codegen.hpp"
#include <string>
#include <vector>

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

rt_divzero:
    mov edi, 2
    lea rsi, [rip + rt_div_msg]
    mov edx, 23
    mov eax, 1
    syscall
    mov edi, 1
    mov eax, 60
    syscall

# 128 bit unsigned divide, one bit per round
# in: rdx:rax dividend, r9:r8 divisor (not zero)
# out: rdx:rax quotient, r11:r10 remainder. clobbers rcx rsi rdi
rt_udivmod128:
    xor r10d, r10d
    xor r11d, r11d
    mov ecx, 128
.Lrt_dm_loop:
    shl rax, 1
    rcl rdx, 1
    rcl r10, 1
    rcl r11, 1
    jc .Lrt_dm_take
    mov rsi, r10
    mov rdi, r11
    sub rsi, r8
    sbb rdi, r9
    jb .Lrt_dm_skip
.Lrt_dm_take:
    sub r10, r8
    sbb r11, r9
    or rax, 1
.Lrt_dm_skip:
    dec rcx
    jnz .Lrt_dm_loop
    ret

# same registers as above. also clobbers r12
rt_sdivmod128:
    xor r12d, r12d
    test rdx, rdx
    jns .Lrt_sd_a
    neg rdx
    neg rax
    sbb rdx, 0
    or r12d, 1
.Lrt_sd_a:
    test r9, r9
    jns .Lrt_sd_b
    neg r9
    neg r8
    sbb r9, 0
    or r12d, 2
.Lrt_sd_b:
    call rt_udivmod128
    test r12d, 1
    jz .Lrt_sd_r
    neg r11
    neg r10
    sbb r11, 0
.Lrt_sd_r:
    mov ecx, r12d
    shr ecx, 1
    xor ecx, r12d
    test ecx, 1
    jz .Lrt_sd_q
    neg rdx
    neg rax
    sbb rdx, 0
.Lrt_sd_q:
    ret

# in: rdx:rax. clobbers r8 r9 r10 r11 r12 r13 and friends
rt_print_u128:
    sub rsp, 48
    lea r13, [rsp + 47]
    mov byte ptr [r13], 10
    mov r8d, 10
    xor r9d, r9d
.Lrt_u128_loop:
    call rt_udivmod128
    add r10b, 48
    dec r13
    mov byte ptr [r13], r10b
    mov r12, rax
    or r12, rdx
    jnz .Lrt_u128_loop
    lea rdx, [rsp + 48]
    sub rdx, r13
    mov rsi, r13
    mov edi, 1
    mov eax, 1
    syscall
    add rsp, 48
    ret

rt_print_i128:
    test rdx, rdx
    jns rt_print_u128
    push rax
    push rdx
    lea rsi, [rip + rt_minus]
    mov edx, 1
    mov edi, 1
    mov eax, 1
    syscall
    pop rdx
    pop rax
    neg rdx
    neg rax
    sbb rdx, 0
    jmp rt_print_u128
)";

const char* RODATA = R"(
.section .rodata
rt_minus: .ascii "-"
rt_bool_txt: .ascii "#f\n#t\n"
rt_trap_msg: .ascii "rill: no pattern matched\n"
rt_div_msg: .ascii "rill: division by zero\n"
)";

bool wide(const IrTy& t) { return !t.is_bool && t.width > 64; }

class Gen {
public:
    explicit Gen(const Module& m) : m_(m) {}

    bool run(std::string& out) {
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

    // off_ is where the register's slot ends, a wide one is 16 bytes with the low word first
    std::string mem(int v, size_t up) const {
        return "qword ptr [rbp - " + std::to_string(off_[static_cast<size_t>(v)] - up) + "]";
    }
    std::string slot(int v) const { return mem(v, 0); }
    std::string hi(int v) const { return mem(v, 8); }

    void load(const char* reg, int v) { line(std::string("mov ") + reg + ", " + slot(v)); }
    void loadh(const char* reg, int v) { line(std::string("mov ") + reg + ", " + hi(v)); }
    void store(int v) { line("mov " + slot(v) + ", rax"); }
    void store2(int v) {
        store(v);
        line("mov " + hi(v) + ", rdx");
    }

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

    // wide values: only the high word has spare bits
    void normalize_hi(const IrTy& t) {
        if (t.width >= 128) return;
        std::string sh = std::to_string(128 - t.width);
        line("shl rdx, " + sh);
        line(std::string(t.sign ? "sar" : "shr") + " rdx, " + sh);
    }

    void function(int fi) {
        const Function& f = m_.fns[static_cast<size_t>(fi)];
        fi_ = fi;
        cf_ = &f;
        off_.assign(f.vregs.size(), 0);
        size_t total = 0;
        for (size_t i = 0; i < f.vregs.size(); ++i) {
            total += wide(f.vregs[i]) ? 16 : 8;
            off_[i] = total;
        }
        out_ += "\n" + fn_label(fi) + ":  # " + f.name + "\n";
        line("push rbp");
        line("mov rbp, rsp");
        line("sub rsp, " + std::to_string((total + 15) / 16 * 16));
        int w = 0;  // words of arguments seen so far
        for (int i = 0; i < f.nparams; ++i) {
            line("mov rax, qword ptr [rbp + " + std::to_string(16 + 8 * w) + "]");
            store(i);
            if (wide(f.vregs[static_cast<size_t>(i)])) {
                line("mov rax, qword ptr [rbp + " + std::to_string(16 + 8 * (w + 1)) + "]");
                line("mov " + hi(i) + ", rax");
                w += 2;
            } else {
                w += 1;
            }
        }
        for (size_t b = 0; b < f.blocks.size(); ++b) {
            out_ += block_label(static_cast<int>(b)) + ":\n";
            for (const Instr& in : f.blocks[b].code) instr(in);
            terminator(f.blocks[b]);
        }
    }

    void call(const Instr& in) {
        size_t words = 0;
        for (size_t i = in.args.size(); i-- > 0;) {
            int a = in.args[i];
            if (wide(cf_->vregs[static_cast<size_t>(a)])) {
                line("push " + hi(a));
                words += 1;
            }
            line("push " + slot(a));
            words += 1;
        }
        line("call " + fn_label(static_cast<int>(in.imm)));
        if (words) line("add rsp, " + std::to_string(8 * words));
        if (in.dst < 0) return;
        if (wide(in.ty)) store2(in.dst);
        else store(in.dst);
    }

    void instr(const Instr& in) {
        // a call's type is the result type, so it has to be looked at before the width check
        if (in.op == Op::Call) { call(in); return; }
        if (wide(in.ty)) { wide_instr(in); return; }

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

    void wide_instr(const Instr& in) {
        const IrTy& t = in.ty;
        switch (in.op) {
            case Op::Const:
                line("movabs rax, " + std::to_string(static_cast<unsigned long long>(in.imm)));
                store(in.dst);
                line("movabs rax, " + std::to_string(static_cast<unsigned long long>(in.imm >> 64)));
                line("mov " + hi(in.dst) + ", rax");
                return;
            case Op::Copy:
                load("rax", in.args[0]);
                loadh("rdx", in.args[0]);
                store2(in.dst);
                return;
            case Op::Neg:
                load("rax", in.args[0]);
                loadh("rdx", in.args[0]);
                line("neg rdx");
                line("neg rax");
                line("sbb rdx, 0");
                normalize_hi(t);
                store2(in.dst);
                return;
            case Op::Print:
                load("rax", in.args[0]);
                loadh("rdx", in.args[0]);
                line(t.sign ? "call rt_print_i128" : "call rt_print_u128");
                return;
            case Op::Lt:
            case Op::Gt: {
                // x < y, and a > b is just b < a
                int x = in.op == Op::Lt ? in.args[0] : in.args[1];
                int y = in.op == Op::Lt ? in.args[1] : in.args[0];
                load("rax", x);
                loadh("rdx", x);
                line("cmp rax, " + slot(y));
                line("sbb rdx, " + hi(y));
                line(t.sign ? "setl al" : "setb al");
                line("movzx eax, al");
                store(in.dst);
                return;
            }
            case Op::Eq:
                load("rax", in.args[0]);
                line("xor rax, " + slot(in.args[1]));
                loadh("rdx", in.args[0]);
                line("xor rdx, " + hi(in.args[1]));
                line("or rax, rdx");
                line("sete al");
                line("movzx eax, al");
                store(in.dst);
                return;
            case Op::Add:
            case Op::Sub: {
                bool add = in.op == Op::Add;
                load("rax", in.args[0]);
                loadh("rdx", in.args[0]);
                line(std::string(add ? "add" : "sub") + " rax, " + slot(in.args[1]));
                line(std::string(add ? "adc" : "sbb") + " rdx, " + hi(in.args[1]));
                break;
            }
            case Op::And:
                load("rax", in.args[0]);
                loadh("rdx", in.args[0]);
                line("and rax, " + slot(in.args[1]));
                line("and rdx, " + hi(in.args[1]));
                break;
            case Op::Mul:
                // low 128 bits of a*b: lo*lo in full, plus the two cross products in the top
                load("rax", in.args[0]);
                line("mul " + slot(in.args[1]));
                line("mov r8, rax");
                line("mov r9, rdx");
                load("rax", in.args[0]);
                line("imul rax, " + hi(in.args[1]));
                line("add r9, rax");
                loadh("rax", in.args[0]);
                line("imul rax, " + slot(in.args[1]));
                line("add r9, rax");
                line("mov rax, r8");
                line("mov rdx, r9");
                break;
            case Op::Shl:
            case Op::Shr: {
                int n = lbl_++;
                std::string skip = ".Lw" + std::to_string(n);
                load("rcx", in.args[1]);
                load("rax", in.args[0]);
                loadh("rdx", in.args[0]);
                if (in.op == Op::Shl) {
                    line("shld rdx, rax, cl");
                    line("shl rax, cl");
                    line("test cl, 64");
                    line("jz " + skip);
                    line("mov rdx, rax");
                    line("xor eax, eax");
                } else {
                    line("shrd rax, rdx, cl");
                    line(t.sign ? "sar rdx, cl" : "shr rdx, cl");
                    line("test cl, 64");
                    line("jz " + skip);
                    line("mov rax, rdx");
                    if (t.sign) line("sar rdx, 63");
                    else line("xor edx, edx");
                }
                out_ += skip + ":\n";
                break;
            }
            case Op::Div:
            case Op::Rem:
                load("rax", in.args[0]);
                loadh("rdx", in.args[0]);
                load("r8", in.args[1]);
                loadh("r9", in.args[1]);
                line("mov rcx, r8");
                line("or rcx, r9");
                line("jz rt_divzero");
                line(t.sign ? "call rt_sdivmod128" : "call rt_udivmod128");
                if (in.op == Op::Rem) {
                    line("mov rax, r10");
                    line("mov rdx, r11");
                }
                break;
            default:
                return;
        }
        normalize_hi(t);
        store2(in.dst);
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
                if (b.val >= 0) {
                    load("rax", b.val);
                    if (wide(cf_->vregs[static_cast<size_t>(b.val)])) loadh("rdx", b.val);
                }
                line("leave");
                line("ret");
                break;
            case Term::Exit:
                load("rdi", b.val);  // a wide exit code just loses its top word
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
    const Function* cf_ = nullptr;
    std::vector<size_t> off_;
    std::string out_;
    int fi_ = 0;
    int lbl_ = 0;
};

}  // namespace

// error stays in the signature, the backend will have things it can't do again
bool codegen_x86(const Module& m, std::string& asm_text, std::string& error) {
    (void)error;
    return Gen(m).run(asm_text);
}

}  // namespace rill
