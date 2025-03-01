// ------------------------------------------------------------------------------------
// semantic analyzer
// ------------------------------------------------------------------------------------

#include "include/analyzer.h"
#include "include/parser.h"
#include "include/error.h"
#include "include/debug.h"
#include <stdlib.h>

bool is_equal_type(Type *lhs, Type *rhs)
{
    if (lhs->type == rhs->type)
    {
        if (lhs->type == TYPE_PTR)
            return is_equal_type(lhs->ptr_to, rhs->ptr_to);
        return true;
    }
    return false;
}

void add_type(Node *node)
{
    if (node->lhs)
        add_type(node->lhs);
    if (node->rhs)
        add_type(node->rhs);
    if (node->condition)
        add_type(node->condition);
    if (node->true_code)
        add_type(node->true_code);
    if (node->false_code) // node->init も同じ
        add_type(node->false_code);
    if (node->update)
        add_type(node->update);
    if (node->expr)
        for (NDBlock *tmp = node->expr; tmp; tmp = tmp->next)
            add_type(tmp->node);
    if (node->stmt)
        for (NDBlock *tmp = node->stmt; tmp; tmp = tmp->next)
            add_type(tmp->node);

    if (node->kind == ND_MUL ||
        node->kind == ND_DIV ||
        node->kind == ND_FUNCCALL)
    {
        node->type = alloc_type(TYPE_INT);
        return;
    }
    if (node->kind == ND_ADDR)
    {
        Type *ptr = alloc_type(TYPE_PTR);
        ptr->ptr_to = node->lhs->type;
        node->lhs->type = ptr;
        node->type = ptr;
        return;
    }
    if (node->kind == ND_DEREF)
    {
        if (node->lhs->type->type != TYPE_PTR)
            error_at(node->token->str, "invalid dereference");
        node->lhs->type = node->lhs->type->ptr_to;
        node->type = node->lhs->type;
        return;
    }
    if (node->kind == ND_ADD)
    {
        int flag = 0;
        Type *type = alloc_type(TYPE_INT);
        if (node->lhs->type->type == TYPE_PTR)
        {
            type = node->lhs->type;
            flag++;
        }
        if (node->rhs->type->type == TYPE_PTR)
        {
            type = node->rhs->type;
            flag += 2;
        }
        switch (flag)
        {
        case 1:
            break;
        case 2:
            break;
        case 3:
            error_at(node->token->str, "invalid use of the '+' operator");
            break;
        default:
            break;
        }
        node->lhs->type = type;
        node->rhs->type = type;
        node->type = type;
        return;
    }
    if (node->kind == ND_SUB)
    {
        if (node->rhs->type->type == TYPE_PTR)
            error_at(node->token->str, "invalid use of the '-' operator");
        if (node->lhs->type->type == TYPE_PTR)
        {
            node->rhs->type = node->lhs->type;
            node->type = node->lhs->type;
        }
        else
            node->type = alloc_type(TYPE_INT);
        return;
    }
    if (node->kind == ND_ASSIGN)
    {
        if (!is_equal_type(node->lhs->type, node->rhs->type))
            error_at(node->token->str, "the types on both sides of '=' must match");
        return;
    }
    if (node->kind == ND_SIZEOF)
    {
        node->type = alloc_type(TYPE_INT);
        return;
    }
}

void analyze_type(Node *node)
{
    if (node->lhs)
        analyze_type(node->lhs);
    if (node->rhs)
        analyze_type(node->rhs);
    if (node->condition)
        analyze_type(node->condition);
    if (node->true_code)
        analyze_type(node->true_code);
    if (node->false_code) // node->init も同じ
        analyze_type(node->false_code);
    if (node->update)
        analyze_type(node->update);
    if (node->expr)
        for (NDBlock *tmp = node->expr; tmp; tmp = tmp->next)
            analyze_type(tmp->node);
    if (node->stmt)
        for (NDBlock *tmp = node->stmt; tmp; tmp = tmp->next)
            analyze_type(tmp->node);

    if (node->kind == ND_NUM)
    {
        if (node->type->type == TYPE_PTR)
        {
            switch (node->type->ptr_to->type)
            {
            case TYPE_PTR:
                node->val = node->val * 8;
                break;
            case TYPE_INT:
                node->val = node->val * 8;
                break;
            default:
                error_exit("unreachable");
                break;
            }
        }
    }
    if (node->kind == ND_SIZEOF)
    {
        node->kind = ND_NUM;
        switch (node->lhs->type->type)
        {
        case TYPE_PTR:
            node->val = 8;
            break;
        case TYPE_INT:
            node->val = 8;
            break;
        default:
            error_exit("unreachable");
            break;
        }
        return;
    }
}

FuncBlock *analyzer(FuncBlock *funcblock)
{
    for (FuncBlock *pointer = funcblock; pointer; pointer = pointer->next)
    {
        Node *node = pointer->node;
        if (node->kind == ND_FUNCDEF)
        {
            for (NDBlock *tmp = node->expr; tmp; tmp = tmp->next)
            {
                add_type(tmp->node);
            }
            for (NDBlock *tmp = node->stmt; tmp; tmp = tmp->next)
            {
                add_type(tmp->node);
            }
        }
    }
#ifdef DEBUG
    print_parse_result(funcblock);
#endif
    for (FuncBlock *pointer = funcblock; pointer; pointer = pointer->next)
    {
        Node *node = pointer->node;
        if (node->kind == ND_FUNCDEF)
        {
            for (NDBlock *tmp = node->expr; tmp; tmp = tmp->next)
            {
                analyze_type(tmp->node);
            }
            for (NDBlock *tmp = node->stmt; tmp; tmp = tmp->next)
            {
                analyze_type(tmp->node);
            }
        }
    }
#ifdef DEBUG
    print_parse_result(funcblock);
#endif
    return funcblock;
}