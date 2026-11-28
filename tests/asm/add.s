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

.section .rodata
rt_minus: .ascii "-"
rt_bool_txt: .ascii "#f\n#t\n"
rt_trap_msg: .ascii "rill: no pattern matched\n"
.section .note.GNU-stack,"",@progbits
