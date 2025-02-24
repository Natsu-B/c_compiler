// ------------------------------------------------------------------------------------
// generator
// ------------------------------------------------------------------------------------

#include "include/generator.h"
#include "include/error.h"
#include <stdio.h>
#include <string.h>

#define error_exit_with_guard(fmt, ...)                                                      \
    do                                                                                       \
    {                                                                                        \
        fprintf(fout, "error detected at %s:%d:%s() exit...", __FILE__, __LINE__, __func__); \
        error_exit(fmt, ##__VA_ARGS__);                                                      \
    } while (0)

void gen_lval(FILE *fout, Node *node)
{
    if (node->kind != ND_LVAR)
        error_exit_with_guard("代入の左辺値が変数でありません");

    fprintf(fout, "    mov rax, rbp\n");
    fprintf(fout, "    sub rax, %d\n", node->offset);
    fprintf(fout, "    push rax\n");
}

int align_counter;

void gen(FILE *fout, Node *node)
{
    if (!node)
        error_exit_with_guard("null pointer");
    if (node->kind == ND_FUNCCALL)
    {
        fprintf(fout, "# start calling %.*s\n", node->func_len, node->func_name);
        NDBlock *pointer = node->expr;
        int i = 0;
        while (pointer)
        {
            gen(fout, pointer->node);
            fprintf(fout, "    pop rax\n");

            switch (++i)
            {
            case 1:
                fprintf(fout, "    mov rdi, rax\n");
                break;
            case 2:
                fprintf(fout, "    mov rsi, rax\n");
                break;
            case 3:
                fprintf(fout, "    mov rdx, rax\n");
                break;
            case 4:
                fprintf(fout, "    mov rcx, rax\n");
                break;
            case 5:
                fprintf(fout, "    mov r8, rax\n");
                break;
            case 6:
                fprintf(fout, "    mov r9, rax\n");
                break;
            default:
                error_exit_with_guard("too much arguments");
                break;
            }
            pointer = pointer->next;
        }
        // rspの16byte alignment 対応
        fprintf(fout, "    mov rax, rsp\n");
        fprintf(fout, "    and rax, 15\n");
        fprintf(fout, "    jnz .L_%d_unaligned\n", align_counter);
        fprintf(fout, "    mov rax, 0\n");
        fprintf(fout, "    call %.*s\n", node->func_len, node->func_name);
        fprintf(fout, "    jmp .L_%d_aligned\n", align_counter);
        fprintf(fout, ".L_%d_unaligned:\n", align_counter);
        fprintf(fout, "    sub rsp, 8\n");
        fprintf(fout, "    mov rax, 0\n");
        fprintf(fout, "    call %.*s\n", node->func_len, node->func_name);
        fprintf(fout, "    add rsp, 8\n");
        fprintf(fout, ".L_%d_aligned:\n", align_counter);
        fprintf(fout, "    push rax\n");
        align_counter++;

        fprintf(fout, "# end calling %.*s\n", node->func_len, node->func_name);
        return;
    }
    if (node->kind == ND_BLOCK)
    {
        for (NDBlock *pointer = node->stmt; pointer; pointer = pointer->next)
        {
            gen(fout, pointer->node);
            fprintf(fout, "    pop rax\n");
        }
        return;
    }
    if (node->kind == ND_IF)
    {
        fprintf(fout, "# start if block\n");
        gen(fout, node->condition);
        fprintf(fout, "    pop rax\n");

        fprintf(fout, "    cmp rax, 0\n");
        fprintf(fout, "    je .Lendif%s\n", node->name->name);
        gen(fout, node->true_code);
        fprintf(fout, ".Lendif%s:\n", node->name->name);
        fprintf(fout, "# end if block\n");
        return;
    }
    if (node->kind == ND_ELIF)
    {
        fprintf(fout, "# start elif block\n");
        gen(fout, node->condition);
        fprintf(fout, "    pop rax\n");

        fprintf(fout, "    cmp rax, 0\n");
        fprintf(fout, "    je .Lelseif%s\n", node->name->name);
        gen(fout, node->true_code);
        fprintf(fout, "    jmp .Lendif%s\n", node->name->name);
        fprintf(fout, ".Lelseif%s:\n", node->name->name);
        gen(fout, node->false_code);
        fprintf(fout, ".Lendif%s:\n", node->name->name);
        fprintf(fout, "# end elif block\n");
        return;
    }
    if (node->kind == ND_WHILE)
    {
        fprintf(fout, "# start while block\n");
        fprintf(fout, ".Lbeginwhile%s:\n", node->name->name);
        gen(fout, node->condition);
        fprintf(fout, "    pop rax\n");

        fprintf(fout, "    cmp rax, 0\n");
        fprintf(fout, "    je .Lendwhile%s\n", node->name->name);
        gen(fout, node->true_code);
        fprintf(fout, "    jmp .Lbeginwhile%s\n", node->name->name);
        fprintf(fout, ".Lendwhile%s:\n", node->name->name);
        fprintf(fout, "# end while block\n");
        return;
    }
    if (node->kind == ND_FOR)
    {
        fprintf(fout, "# start for block\n");
        gen(fout, node->init);
        fprintf(fout, ".Lbeginfor%s:\n", node->name->name);
        gen(fout, node->condition);
        fprintf(fout, "    pop rax\n");

        fprintf(fout, "    cmp rax, 0\n");
        fprintf(fout, "    je .Lendfor%s\n", node->name->name);
        gen(fout, node->true_code);
        gen(fout, node->update);
        fprintf(fout, "    jmp .Lbeginfor%s\n", node->name->name);
        fprintf(fout, ".Lendfor%s:\n", node->name->name);
        fprintf(fout, "# end for block\n");
        return;
    }
    if (node->kind == ND_RETURN)
    {
        gen(fout, node->rhs);
        fprintf(fout, "    pop rax\n");

        fprintf(fout, "    mov rsp, rbp\n");
        fprintf(fout, "    pop rbp\n");
        fprintf(fout, "    ret\n");
        return;
    }
    switch (node->kind)
    {
    case ND_NUM:
        fprintf(fout, "    push %ld\n", node->val);

        return;
    case ND_LVAR:
        gen_lval(fout, node);
        fprintf(fout, "    pop rax\n");

        fprintf(fout, "    mov rax, [rax]\n");
        fprintf(fout, "    push rax\n");

        return;
    case ND_ASSIGN:
        gen_lval(fout, node->lhs);
        gen(fout, node->rhs);
        fprintf(fout, "    pop rdi\n");

        fprintf(fout, "    pop rax\n");

        fprintf(fout, "    mov [rax], rdi\n");
        fprintf(fout, "    push rdi\n");

        return;
    default:
        break;
    }

    gen(fout, node->lhs);
    gen(fout, node->rhs);

    fprintf(fout, "    pop rdi\n");

    fprintf(fout, "    pop rax\n");

    switch (node->kind)
    {
    case ND_ADD:
        fprintf(fout, "    add rax, rdi\n");
        break;
    case ND_SUB:
        fprintf(fout, "    sub rax, rdi\n");
        break;
    case ND_MUL:
        fprintf(fout, "    imul rax, rdi\n");
        break;
    case ND_DIV:
        fprintf(fout, "    cqo\n");
        fprintf(fout, "    idiv rdi\n");
        break;
    case ND_EQ:
        fprintf(fout, "    cmp rax, rdi\n");
        fprintf(fout, "    sete al\n");
        fprintf(fout, "    movzb rax, al\n");
        break;
    case ND_NEQ:
        fprintf(fout, "    cmp rax, rdi\n");
        fprintf(fout, "    setne al\n");
        fprintf(fout, "    movzb rax, al\n");
        break;
    case ND_LT:
        fprintf(fout, "    cmp rax, rdi\n");
        fprintf(fout, "    setl al\n");
        fprintf(fout, "    movzb rax, al\n");
        break;
    case ND_LTE:
        fprintf(fout, "    cmp rax, rdi\n");
        fprintf(fout, "    setle al\n");
        fprintf(fout, "    movzb rax, al\n");
        break;
    default:
        error_exit_with_guard("unreachable");
        break;
    }

    fprintf(fout, "    push rax\n");
}

void generator(FuncBlock *parsed, char *output_filename)
{
    pr_debug("start generator");

    pr_debug("output filename: %.*s", strlen(output_filename), output_filename);
    FILE *fout = fopen(output_filename, "w");
    if (fout == NULL)
    {
        error_exit("ファイルに書き込めませんでした");
    }
    pr_debug("output file open");
    fprintf(fout, ".intel_syntax noprefix\n");
    fprintf(fout, ".global main\n");

    int i = 0;
    for (FuncBlock *pointer = parsed; pointer; pointer = pointer->next)
    {
        pr_debug2("code[%d]", i++);
        Node *node = pointer->node;
        if (node->kind == ND_FUNCDEF)
        {
            fprintf(fout, "\n%.*s:\n", node->func_len, node->func_name);
            fprintf(fout, "    push rbp\n");
            fprintf(fout, "    mov rbp, rsp\n");
            fprintf(fout, "    sub rsp, %d\n", pointer->stacksize * 8);
            int j = 0;
            for (NDBlock *pointer = node->expr; pointer; pointer = pointer->next)
            {
                if (pointer->node->kind != ND_LVAR)
                    error_exit_with_guard("invalid function arguments");
                fprintf(fout, "    mov rax, rbp\n");
                fprintf(fout, "    sub rax, %d\n", pointer->node->offset);
                switch (++j)
                {
                case 1:
                    fprintf(fout, "    mov [rax], rdi\n");
                    break;
                case 2:
                    fprintf(fout, "    mov [rax], rsi\n");
                    break;
                case 3:
                    fprintf(fout, "    mov [rax], rdx\n");
                    break;
                case 4:
                    fprintf(fout, "    mov [rax], rcx\n");
                    break;
                case 5:
                    fprintf(fout, "    mov [rax], r8\n");
                    break;
                case 6:
                    fprintf(fout, "    mov [rax], r9\n");
                    break;
                default:
                    error_exit_with_guard("too much arguments");
                    break;
                }
            }

            align_counter = 0;
            for (NDBlock *pointer = node->stmt; pointer; pointer = pointer->next)
                gen(fout, pointer->node);
        }
        else
            error_exit_with_guard("unreachable");
        fprintf(fout, "    pop rax\n");
    }

    fclose(fout);
    pr_debug("complite generate");
}