#include "include/debug.h"
#include "include/error.h"
#include "include/tokenizer.h"
#include "include/parser.h"
#include <stdio.h>

void print_tokenize_result(Token *token, char **tokenkindlist)
{
    pr_debug("tokenize result:");
    for (;;)
    {
        if (token->kind == TK_EOF)
            break;
        if (token->kind == TK_NUM)
            fprintf(stdout, "%ld\t: %s\n", token->val, tokenkindlist[token->kind]);
        else
            fprintf(stdout, "%.*s\t: %s\n", token->len, token->str, tokenkindlist[token->kind]);
        token = token->next;
    }
}

void make_space(int nest)
{
    for (int i = 0; i < nest; i++)
        fprintf(stdout, "|   ");
}

void _print_parse_result(Node *node, int nest, char **nodekindlist)
{
    make_space(nest);
    if (node->kind == ND_NUM)
        fprintf(stdout, "NodeKind: %s value: %ld\n", nodekindlist[node->kind], node->val);
    else if (node->kind == ND_LVAR)
        fprintf(stdout, "NodeKind: %s offset: %d\n", nodekindlist[node->kind], node->offset);
    else if (node->kind == ND_IF || node->kind == ND_ELIF || node->kind == ND_WHILE)
    {
        fprintf(stdout, "NodeKind: %s labelname%s\n", nodekindlist[node->kind], node->name->name);
        make_space(nest);
        fprintf(stdout, "|   [condition]\n");
        _print_parse_result(node->condition, nest + 1, nodekindlist);
        make_space(nest);
        fprintf(stdout, "|   [true_code]\n");
        _print_parse_result(node->true_code, nest + 1, nodekindlist);
        if (node->false_code)
        {
            make_space(nest);
            fprintf(stdout, "|    [false_code]\n");
            _print_parse_result(node->false_code, nest + 1, nodekindlist);
        }
    }
    else if (node->kind == ND_FOR)
    {
        fprintf(stdout, "NodeKind: %s\n", nodekindlist[node->kind]);
        if (node->init)
        {
            make_space(nest);
            fprintf(stdout, "|   [init]\n");
            _print_parse_result(node->init, nest + 1, nodekindlist);
        }
        if (node->condition)
        {
            make_space(nest);
            fprintf(stdout, "|   [condition]\n");
            _print_parse_result(node->condition, nest + 1, nodekindlist);
        }
        if (node->update)
        {
            make_space(nest);
            fprintf(stdout, "|   [update]\n");
            _print_parse_result(node->update, nest + 1, nodekindlist);
        }
        if (node->true_code)
        {
            make_space(nest);
            fprintf(stdout, "|   [code]\n");
            _print_parse_result(node->true_code, nest+1, nodekindlist);
        }
    }
    else
    {
        fprintf(stdout, "NodeKind: %s\n", nodekindlist[node->kind]);
        if (node->lhs)
        {
            make_space(nest);
            fprintf(stdout, "|   [lhs]\n");
            _print_parse_result(node->lhs, nest + 1, nodekindlist);
        }
        if (node->rhs)
        {
            make_space(nest);
            fprintf(stdout, "|   [rhs]\n");
            _print_parse_result(node->rhs, nest + 1, nodekindlist);
        }
    }
}

void print_parse_result(Node **node, int i, char **nodekindlist)
{
    pr_debug("parse result:");
    for (int j = 0; j < i; j++)
    {
        fprintf(stdout, "node[%d]", j);
        _print_parse_result(node[j], 0, nodekindlist);
    }
}