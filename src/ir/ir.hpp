#pragma once
#include <string>
#include <vector>
#include "common/num.hpp"

namespace rill {

struct IrTy {
    int width = 64;
    bool sign = true;
    bool is_bool = false;  // a bool is one bit wide, but it prints as #t/#f
    bool operator==(const IrTy&) const = default;
};

std::string ty_str(const IrTy& t);

enum class Op {
    Const, Copy,
    Add, Sub, Mul, Div, Rem, And, Shl, Shr, Neg,
    Lt, Gt, Eq,
    Call, Print, PrintStr,
    LoadG, StoreG
};

struct Instr {
    Op op = Op::Copy;
    int dst = -1;           // -1 when the op gives no value
    IrTy ty;                // what the op works on. for Lt/Gt/Eq that is the operands, not the bool
    std::vector<int> args;  // registers
    u128 imm = 0;           // Const: the value. Call: function index. PrintStr: string index. LoadG, StoreG: global index
};

enum class Term { None, Jmp, Br, Ret, Exit, Trap };

struct Block {
    std::vector<Instr> code;
    Term term = Term::None;
    int val = -1;    // Br: the condition. Ret and Exit: the value
    int to = -1;     // Jmp: target. Br: target when true
    int other = -1;  // Br: target when false
};

struct Function {
    std::string name;
    std::vector<IrTy> vregs;  // type of every register
    int nparams = 0;          // registers 0..nparams-1 are the parameters
    bool has_ret = false;
    IrTy ret;
    std::vector<Block> blocks;
};

struct Module {
    std::vector<Function> fns;  // fns[0] is main
    std::vector<std::string> strings;
    std::vector<IrTy> globals;  // memory cells, read with LoadG and written with StoreG
};

std::string ir_dump(const Module& m);

}  // namespace rill
