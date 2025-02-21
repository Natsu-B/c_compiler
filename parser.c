// ------------------------------------------------------------------------------------
// parser
// ------------------------------------------------------------------------------------

#include "include/parser.h"
#include "include/tokenizer.h"
#include "include/error.h"
#include <stdlib.h>

Node *new_node(NodeKind kind, Node *lhs, Node *rhs)
{
    Node *node = calloc(1, sizeof(Node));
    node->kind = kind;
    node->lhs = lhs;
    node->rhs = rhs;

    return node;
}

Node *new_node_num(int val)
{
    Node *node = calloc(1, sizeof(Node));
    node->kind = ND_NUM;
    node->val = val;
    return node;
}

// expr       = equality
Node *expr();

// equality   = relational ("==" relational | "!=" relational)*
Node *equality();

// relational = add ("<" add | "<=" add | ">" add | ">=" add)*
Node *relational();

// add        = mul ("+" mul | "-" mul)*
Node *add();

// mul     = unary ("*" unary | "/" unary)*
Node *mul();

// unary   = ("+" | "-")? primary
Node *unary();

// primary = num | "(" expr ")"
Node *primary();

Node *primary()
{
    pr_debug2("primary");
    if (consume("("))
    {
        Node *node = expr();
        expect(")");
        return node;
    }

    return new_node_num(expect_number());
}

Node *unary()
{
    pr_debug2("unary");
    if (consume("+"))
        return primary();
    if (consume("-"))
        return new_node(ND_SUB, new_node_num(0), primary());
    return primary();
}

Node *mul()
{
    pr_debug2("mul");
    Node *node = unary();

    for (;;)
    {
        if (consume("*"))
            node = new_node(ND_MUL, node, unary());
        else if (consume("/"))
            node = new_node(ND_DIV, node, unary());
        else
            return node;
    }
}

Node *add()
{
    pr_debug2("add");
    Node *node = mul();

    for (;;)
    {
        if (consume("+"))
            node = new_node(ND_ADD, node, mul());
        else if (consume("-"))
            node = new_node(ND_SUB, node, mul());
        else
            return node;
    }
}

Node *relational()
{
    pr_debug2("relational");
    Node *node = add();
    for (;;)
    {
        if (consume("<="))
            node = new_node(ND_LTE, node, add());
        else if (consume("<"))
            node = new_node(ND_LT, node, add());
        else if (consume(">="))
            node = new_node(ND_LTE, add(), node);
        else if (consume(">"))
            node = new_node(ND_LT, add(), node);
        else
            return node;
    }
}

inline Node *equality()
{
    pr_debug2("equality");
    Node *node = relational();

    for (;;)
    {
        if (consume("=="))
            node = new_node(ND_EQ, node, relational());
        else if (consume("!="))
            node = new_node(ND_NEQ, node, relational());
        else
            return node;
    }
}

inline Node *expr()
{
    return equality();
}

Node *parser()
{
    pr_debug("start parser...");
    Node *result = expr();
    pr_debug("complite parse");
    return result;
}
