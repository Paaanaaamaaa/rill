.intel_syntax noprefix
.text
.globl _start
_start:
    call rl_main
    ud2

rl_main:  # main
    push rbp
    mov rbp, rsp
    sub rsp, 48
.Lf0_b0:
    movabs rax, 5
    mov qword ptr [rbp - 8], rax
    mov rax, qword ptr [rbp - 8]
    mov qword ptr [rbp - 16], rax
    movabs rax, 1
    mov qword ptr [rbp - 24], rax
    mov rax, qword ptr [rbp - 16]
    mov rcx, qword ptr [rbp - 24]
    add rax, rcx
    mov qword ptr [rbp - 32], rax
    mov rdi, qword ptr [rbp - 32]
    call rt_print_i
    movabs rax, 0
    mov qword ptr [rbp - 40], rax
    mov rdi, qword ptr [rbp - 40]
    mov eax, 60
    syscall

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

.section .rodata
rt_minus: .ascii "-"
rt_bool_txt: .ascii "#f\n#t\n"
rt_trap_msg: .ascii "rill: no pattern matched\n"
rt_div_msg: .ascii "rill: division by zero\n"
.section .note.GNU-stack,"",@progbits
