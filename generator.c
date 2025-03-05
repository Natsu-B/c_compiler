// ------------------------------------------------------------------------------------
// generator
// ------------------------------------------------------------------------------------

#include "include/generator.h"
#include "include/error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define error_exit_with_guard(fmt, ...)                                                    \
    do                                                                                     \
    {                                                                                      \
        output_file("error detected at %s:%d:%s() exit...", __FILE__, __LINE__, __func__); \
        error_exit(fmt, ##__VA_ARGS__);                                                    \
    } while (0)

FILE *fout;

#define output_file(fmt, ...)                   \
    do                                          \
    {                                           \
        fprintf(fout, fmt "\n", ##__VA_ARGS__); \
    } while (0)

// TYPE_INT TYPE_ARRAY 等を受け取ってその大きさを返す関数
int size_of(TypeKind type)
{
    switch (type)
    {
    case TYPE_INT:
        return 4;
    case TYPE_LONG:
        return 8;
    case TYPE_PTR:
        return 8;
    case TYPE_ARRAY:
        return 8;
    }
    error_exit("unreachable");
    return 0; // unreachable
}

typedef enum
{
    rax,
    rcx,
    rdx,
    rbx,
    rsp,
    rbp,
    rsi,
    rdi,
    r8,
    r9,
    r10,
    r11,
    r12,
    r13,
    r14,
    r15,
} register_type;

char *chose_register(int size, register_type reg_type)
{
    switch (size)
    {
    case 1:
    case 2:
        break;
    case 4:
        size = 3;
        break;
    case 8:
        size = 4;
        break;
    default:
        error_exit_with_guard("unknown register size");
        break;
    }
    char *tmp;
    int type = reg_type * 4 + size;
    switch (type)
    {
    case 1:
        tmp = "al";
        break;
    case 2:
        tmp = "ax";
        break;
    case 3:
        tmp = "eax";
        break;
    case 4:
        tmp = "rax";
        break;
    case 5:
        tmp = "cl";
        break;
    case 6:
        tmp = "cx";
        break;
    case 7:
        tmp = "ecx";
        break;
    case 8:
        tmp = "rcx";
        break;
    case 9:
        tmp = "dl";
        break;
    case 10:
        tmp = "dx";
        break;
    case 11:
        tmp = "edx";
        break;
    case 12:
        tmp = "rdx";
        break;
    case 13:
        tmp = "bl";
        break;
    case 14:
        tmp = "bx";
        break;
    case 15:
        tmp = "ebx";
        break;
    case 16:
        tmp = "rbx";
        break;
    case 17:
        tmp = "spl";
        break;
    case 18:
        tmp = "sp";
        break;
    case 19:
        tmp = "esp";
        break;
    case 20:
        tmp = "rsp";
        break;
    case 21:
        tmp = "bpl";
        break;
    case 22:
        tmp = "bp";
        break;
    case 23:
        tmp = "ebp";
        break;
    case 24:
        tmp = "rbp";
        break;
    case 25:
        tmp = "sil";
        break;
    case 26:
        tmp = "si";
        break;
    case 27:
        tmp = "esi";
        break;
    case 28:
        tmp = "rsi";
        break;
    case 29:
        tmp = "dil";
        break;
    case 30:
        tmp = "di";
        break;
    case 31:
        tmp = "edi";
        break;
    case 32:
        tmp = "rdi";
        break;
    case 33:
        tmp = "r8b";
        break;
    case 34:
        tmp = "r8w";
        break;
    case 35:
        tmp = "r8d";
        break;
    case 36:
        tmp = "r8";
        break;
    case 37:
        tmp = "r9b";
        break;
    case 38:
        tmp = "r9w";
        break;
    case 39:
        tmp = "r9d";
        break;
    case 40:
        tmp = "r9";
        break;
    case 41:
        tmp = "r10b";
        break;
    case 42:
        tmp = "r10w";
        break;
    case 43:
        tmp = "r10d";
        break;
    case 44:
        tmp = "r10";
        break;
    case 45:
        tmp = "r11b";
        break;
    case 46:
        tmp = "r11w";
        break;
    case 47:
        tmp = "r11d";
        break;
    case 48:
        tmp = "r11";
        break;
    case 49:
        tmp = "r12b";
        break;
    case 50:
        tmp = "r12w";
        break;
    case 51:
        tmp = "r12d";
        break;
    case 52:
        tmp = "r12";
        break;
    case 53:
        tmp = "r13b";
        break;
    case 54:
        tmp = "r13w";
        break;
    case 55:
        tmp = "r13d";
        break;
    case 56:
        tmp = "r13";
        break;
    case 57:
        tmp = "r14b";
        break;
    case 58:
        tmp = "r14w";
        break;
    case 59:
        tmp = "r14d";
        break;
    case 60:
        tmp = "r14";
        break;
    case 61:
        tmp = "r15b";
        break;
    case 62:
        tmp = "r15w";
        break;
    case 63:
        tmp = "r15d";
        break;
    case 64:
        tmp = "r15";
        break;
    default:
        error_exit_with_guard("unknown register type");
        break;
    }
    char *register_name = calloc(1, sizeof(char) * 5);
    strncpy(register_name, tmp, 5);
    return register_name;
}

void gen(Node *node);

void gen_lval(Node *node)
{
    if (node->kind == ND_LVAR)
    {
        output_file("    lea rax, [rbp-%d]", node->offset);
        output_file("    push rax");
        return;
    }
    if (node->kind == ND_DEREF)
    {
        gen(node->lhs);
        return;
    }
    error_exit_with_guard("代入の左辺値が変数でありません");
}

int align_counter;

void gen(Node *node)
{
    if (!node)
        error_exit_with_guard("null pointer");
    if (node->kind == ND_FUNCCALL)
    {
        output_file("# start calling %.*s", node->func_len, node->func_name);
        NDBlock *pointer = node->expr;
        int i = 0;
        while (pointer)
        {
            gen(pointer->node);
            output_file("    pop rax");

            switch (++i)
            {
            case 1:
                output_file("    mov rdi, rax");
                break;
            case 2:
                output_file("    mov rsi, rax");
                break;
            case 3:
                output_file("    mov rdx, rax");
                break;
            case 4:
                output_file("    mov rcx, rax");
                break;
            case 5:
                output_file("    mov r8, rax");
                break;
            case 6:
                output_file("    mov r9, rax");
                break;
            default:
                error_exit_with_guard("too much arguments");
                break;
            }
            pointer = pointer->next;
        }
        // rspの16byte alignment 対応
        output_file("    mov rax, rsp");
        output_file("    and rax, 15");
        output_file("    jnz .L_%d_unaligned", align_counter);
        output_file("    mov rax, 0");
        output_file("    call %.*s", node->func_len, node->func_name);
        output_file("    jmp .L_%d_aligned", align_counter);
        output_file(".L_%d_unaligned:", align_counter);
        output_file("    sub rsp, 8");
        output_file("    mov rax, 0");
        output_file("    call %.*s", node->func_len, node->func_name);
        output_file("    add rsp, 8");
        output_file(".L_%d_aligned:", align_counter);
        output_file("    push rax");
        align_counter++;

        output_file("# end calling %.*s", node->func_len, node->func_name);
        return;
    }
    if (node->kind == ND_ARRAY)
    {
        gen(node->lhs);
        return;
    }
    if (node->kind == ND_DISCARD_EXPR)
    {
        gen(node->lhs);
        output_file("    add rsp, 8");
        return;
    }
    if (node->kind == ND_BLOCK)
    {
        for (NDBlock *pointer = node->stmt; pointer; pointer = pointer->next)
        {
            gen(pointer->node);
        }
        return;
    }
    if (node->kind == ND_IF)
    {
        output_file("# start if block");
        gen(node->condition);
        output_file("    pop rax");

        output_file("    cmp rax, 0");
        output_file("    je .Lendif%s", node->name->name);
        gen(node->true_code);
        output_file(".Lendif%s:", node->name->name);
        output_file("# end if block");
        return;
    }
    if (node->kind == ND_ELIF)
    {
        output_file("# start elif block");
        gen(node->condition);
        output_file("    pop rax");

        output_file("    cmp rax, 0");
        output_file("    je .Lelseif%s", node->name->name);
        gen(node->true_code);
        output_file("    jmp .Lendif%s", node->name->name);
        output_file(".Lelseif%s:", node->name->name);
        gen(node->false_code);
        output_file(".Lendif%s:", node->name->name);
        output_file("# end elif block");
        return;
    }
    if (node->kind == ND_WHILE)
    {
        output_file("# start while block");
        output_file(".Lbeginwhile%s:", node->name->name);
        gen(node->condition);
        output_file("    pop rax");

        output_file("    cmp rax, 0");
        output_file("    je .Lendwhile%s", node->name->name);
        gen(node->true_code);
        output_file("    jmp .Lbeginwhile%s", node->name->name);
        output_file(".Lendwhile%s:", node->name->name);
        output_file("# end while block");
        return;
    }
    if (node->kind == ND_FOR)
    {
        output_file("# start for block");
        gen(node->init);
        output_file(".Lbeginfor%s:", node->name->name);
        gen(node->condition);
        output_file("    pop rax");

        output_file("    cmp rax, 0");
        output_file("    je .Lendfor%s", node->name->name);
        gen(node->true_code);
        gen(node->update);
        output_file("    jmp .Lbeginfor%s", node->name->name);
        output_file(".Lendfor%s:", node->name->name);
        output_file("# end for block");
        return;
    }
    if (node->kind == ND_RETURN)
    {
        gen(node->rhs);
        output_file("    pop rax");
        output_file("    mov rsp, rbp");
        output_file("    pop rbp");
        output_file("    ret");
        return;
    }
    switch (node->kind)
    {
    case ND_NUM:
        output_file("    push %ld", node->val);
        return;
    case ND_LVAR:
        gen_lval(node);
        output_file("    pop rax");
        if (node->type->type == TYPE_INT)
            output_file("    movsxd rax, DWORD PTR [rax]");
        else
            output_file("    mov rax, [rax]");
        output_file("    push rax");
        return;
    case ND_ASSIGN:
        gen_lval(node->lhs);
        gen(node->rhs);
        output_file("    pop rdi");
        output_file("    pop rax");
        output_file("    mov [rax], %s", chose_register(size_of(node->type->type), rdi));
        output_file("    push rdi");
        return;
    case ND_ADDR:
        gen_lval(node->lhs);
        return;
    case ND_DEREF:
        gen(node->lhs);
        output_file("    pop rax");
        if (node->type->type == TYPE_INT)
            output_file("    movsxd rax, DWORD PTR [rax]");
        else
            output_file("    mov rax, [rax]");
        output_file("    push rax");
        return;
    default:
        break;
    }

    gen(node->lhs);
    gen(node->rhs);

    output_file("    pop rdi");
    output_file("    pop rax");
    switch (node->kind)
    {
    case ND_ADD:
        output_file("    add rax, rdi");
        break;
    case ND_SUB:
        output_file("    sub rax, rdi");
        break;
    case ND_MUL:
        output_file("    imul rax, rdi");
        break;
    case ND_DIV:
        output_file("    cqo");
        output_file("    idiv rdi");
        break;
    case ND_EQ:
        output_file("    cmp rax, rdi");
        output_file("    sete al");
        output_file("    movzb rax, al");
        break;
    case ND_NEQ:
        output_file("    cmp rax, rdi");
        output_file("    setne al");
        output_file("    movzb rax, al");
        break;
    case ND_LT:
        output_file("    cmp rax, rdi");
        output_file("    setl al");
        output_file("    movzb rax, al");
        break;
    case ND_LTE:
        output_file("    cmp rax, rdi");
        output_file("    setle al");
        output_file("    movzb rax, al");
        break;
    default:
        error_exit_with_guard("unreachable");
        break;
    }

    output_file("    push rax");
}

void generator(FuncBlock *parsed, char *output_filename)
{
    pr_debug("start generator");

    pr_debug("output filename: %.*s", strlen(output_filename), output_filename);
    fout = fopen(output_filename, "w");
    if (fout == NULL)
    {
        error_exit("ファイルに書き込めませんでした");
    }
    pr_debug("output file open");
    output_file(".intel_syntax noprefix");
    output_file(".data");
    output_file(".text");

    int i = 0;
    for (FuncBlock *pointer = parsed; pointer; pointer = pointer->next)
    {
        pr_debug2("code[%d]", i++);
        Node *node = pointer->node;
        if (node->kind == ND_FUNCDEF)
        {
            output_file("\n.global %.*s", node->func_len, node->func_name);
            output_file("%.*s:", node->func_len, node->func_name);
            output_file("    push rbp");
            output_file("    mov rbp, rsp");
            output_file("    sub rsp, %d", pointer->stacksize);
            if (pointer->stacksize > 30)
                error_exit_with_guard("too many stack used!!!");
            int j = 0;
            for (NDBlock *pointer = node->expr; pointer; pointer = pointer->next)
            {
                if (pointer->node->kind != ND_LVAR)
                    error_exit_with_guard("invalid function arguments");
                switch (++j)
                {
                case 1:
                    output_file("    mov [rbp-%d], %s", pointer->node->offset, chose_register(size_of(pointer->node->type->type), rdi));
                    break;
                case 2:
                    output_file("    mov [rbp-%d], %s", pointer->node->offset, chose_register(size_of(pointer->node->type->type), rsi));
                    break;
                case 3:
                    output_file("    mov [rbp-%d], %s", pointer->node->offset, chose_register(size_of(pointer->node->type->type), rdx));
                    break;
                case 4:
                    output_file("    mov [rbp-%d], %s", pointer->node->offset, chose_register(size_of(pointer->node->type->type), rcx));
                    break;
                case 5:
                    output_file("    mov [rbp-%d], %s", pointer->node->offset, chose_register(size_of(pointer->node->type->type), r8));
                    break;
                case 6:
                    output_file("    mov [rbp-%d], %s", pointer->node->offset, chose_register(size_of(pointer->node->type->type), r9));
                    break;
                default:
                    error_exit_with_guard("too much arguments");
                    break;
                }
            }

            align_counter = 0;
            for (NDBlock *pointer = node->stmt; pointer; pointer = pointer->next)
                gen(pointer->node);
            output_file("    pop rax");
            output_file("    mov rsp, rbp");
            output_file("    pop rbp");
            output_file("    ret");
        }
        else
            error_exit_with_guard("unreachable");
    }

    fclose(fout);
    pr_debug("complite generate");
}