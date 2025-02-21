// ------------------------------------------------------------------------------------
// generator
// ------------------------------------------------------------------------------------

#include "include/generator.h"
#include "include/error.h"
#include <stdio.h>

void gen(FILE *fout, Node *node)
{
    if (node->kind == ND_NUM)
    {
        fprintf(fout, "    push %d\n", node->val);
        return;
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
        error_exit("unreachable");
        break;
    }

    fprintf(fout, "    push rax\n");
}

void generator(Node *node, char *output_filename)
{
    pr_debug("start generator");
    pr_debug("output filename: %s", output_filename);
    FILE *fout = fopen(output_filename, "w");
    if (fout == NULL)
    {
        error_exit("ファイルに書き込めませんでした");
    }
    pr_debug("output file open");
    fprintf(fout, ".intel_syntax noprefix\n");
    fprintf(fout, ".global main\n");
    fprintf(fout, "main:\n");

    gen(fout, node);

    fprintf(fout, "    pop rax\n");
    fprintf(fout, "    ret\n");

    fclose(fout);
    pr_debug("complite generate");
}