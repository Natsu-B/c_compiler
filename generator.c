// ------------------------------------------------------------------------------------
// generator
// ------------------------------------------------------------------------------------

#include "include/generator.h"
#include "include/error.h"
#include <stdio.h>
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

void gen_lval(Node *node)
{
    if (node->kind == ND_LVAR)
    {
        output_file("    mov rax, rbp");
        output_file("    sub rax, %d", node->offset);
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
        output_file("    mov rax, [rax]");
        output_file("    push rax");
        return;
    case ND_ASSIGN:
        gen_lval(node->lhs);
        gen(node->rhs);
        output_file("    pop rdi");
        output_file("    pop rax");
        output_file("    mov [rax], rdi");
        output_file("    push rdi");
        return;
    case ND_ADDR:
        gen_lval(node->lhs);
        return;
    case ND_DEREF:
        gen(node->lhs);
        output_file("    pop rax");
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
    output_file(".global main");

    int i = 0;
    for (FuncBlock *pointer = parsed; pointer; pointer = pointer->next)
    {
        pr_debug2("code[%d]", i++);
        Node *node = pointer->node;
        if (node->kind == ND_FUNCDEF)
        {
            output_file("\n%.*s:", node->func_len, node->func_name);
            output_file("    push rbp");
            output_file("    mov rbp, rsp");
            output_file("    sub rsp, %d", pointer->stacksize * 8);
            int j = 0;
            for (NDBlock *pointer = node->expr; pointer; pointer = pointer->next)
            {
                if (pointer->node->kind != ND_LVAR)
                    error_exit_with_guard("invalid function arguments");
                output_file("    mov rax, rbp");
                output_file("    sub rax, %d", pointer->node->offset);
                switch (++j)
                {
                case 1:
                    output_file("    mov [rax], rdi");
                    break;
                case 2:
                    output_file("    mov [rax], rsi");
                    break;
                case 3:
                    output_file("    mov [rax], rdx");
                    break;
                case 4:
                    output_file("    mov [rax], rcx");
                    break;
                case 5:
                    output_file("    mov [rax], r8");
                    break;
                case 6:
                    output_file("    mov [rax], r9");
                    break;
                default:
                    error_exit_with_guard("too much arguments");
                    break;
                }
            }

            align_counter = 0;
            for (NDBlock *pointer = node->stmt; pointer; pointer = pointer->next)
                gen(pointer->node);
        }
        else
            error_exit_with_guard("unreachable");
    }

    fclose(fout);
    pr_debug("complite generate");
}