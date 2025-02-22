// ------------------------------------------------------------------------------------
// parser
// ------------------------------------------------------------------------------------

#include "include/parser.h"
#include "include/tokenizer.h"
#include "include/error.h"
#include "include/debug.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

LVar *locals;

LVar *find_lvar(Token *token)
{
    for (LVar *var = locals; var; var = var->next)
        if (var->len == token->len && !memcmp(var->name, token->str, var->len))
            return var;
    return NULL;
}

GTLabel *head_label;

GTLabel *generate_label_name(char *name)
{
    size_t namesize = strlen(name) + 4;
    GTLabel *next = calloc(1, sizeof(GTLabel));
    next->len = namesize;
    next->next = head_label;
    char *mangle_name = malloc(namesize);
    int i = 0;
    for (;;)
    {
        snprintf(mangle_name, namesize, "_%d_%s", i++, name);
        GTLabel *var = head_label;
        for (;;)
        {
            if (!var || !var->next)
            {
                next->name = mangle_name;
                head_label = next;
                return next;
            }
            if (namesize == var->len &&
                !strncmp(mangle_name, var->name, namesize))
            {
                break;
            }

            var = var->next;
        }
    }
}

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

/**
 * program    = stmt*
 * stmt    = expr ";"
 *         | "if" "(" expr ")" stmt ("else" stmt)?
 *         | "while" "(" expr ")" stmt
 *         | "for" "(" expr? ";" expr? ";" expr? ")" stmt
 *         | "return" expr ";"
 * expr       = assign
 * assign     = equality ("=" assign)?
 * equality   = relational ("==" relational | "!=" relational)*
 * relational = add ("<" add | "<=" add | ">" add | ">=" add)*
 * add        = mul ("+" mul | "-" mul)*
 * mul        = unary ("*" unary | "/" unary)*
 * unary      = ("+" | "-")? primary
 * primary    = num | ident | "(" expr ")"
 */

void program();
Node *stmt();
Node *expr();
Node *assign();
Node *equality();
Node *relational();
Node *add();
Node *mul();
Node *unary();
Node *primary();

Node *code[max_num];

void parser()
{
    pr_debug("start parser...");
    program();
    pr_debug("complite parse");
}

void program()
{
    pr_debug2("program");
    int i = 0;
    while (!at_eof())
    {
        code[i++] = stmt();
        if (i == max_num)
            error_exit("行数が多すぎます");
    }

#ifdef DEBUG
    char *NodeKindList[ND_END] = {NodeKindTable};
    print_parse_result(code, i, NodeKindList);
#endif
}

Node *stmt()
{
    pr_debug2("stmt");
    // if文の判定とelseがついてるかどうか
    if (consume_TokenKind(TK_IF))
    {
        Node *node = calloc(1, sizeof(Node));
        node->name = generate_label_name("main");
        expect("(");
        node->condition = expr();
        expect(")");
        if (peek_next_TokenKind(TK_ELSE))
        {
            node->kind = ND_ELIF;
            node->true_code = stmt();
            if (!consume_TokenKind(TK_ELSE))
                error_exit("unreachable");
            node->false_code = stmt();
        }
        else
        {
            node->kind = ND_IF;
            node->true_code = stmt();
        }
        return node;
    }

    // while文の判定
    if (consume_TokenKind(TK_WHILE))
    {
        Node *node = calloc(1, sizeof(Node));
        node->kind = ND_WHILE;
        node->name = generate_label_name("main");
        expect("(");
        node->condition = expr();
        expect(")");
        node->true_code = stmt();
        return node;
    }

    // for文の判定
    if (consume_TokenKind(TK_FOR))
    {
        Node *node = calloc(1, sizeof(Node));
        node->kind = ND_FOR;
        node->name = generate_label_name("main");
        expect("(");
        if (!consume(";"))
        {
            node->init = expr();
            expect(";");
        }
        if (!consume(";"))
        {
            node->condition = expr();
            expect(";");
        }
        if (!consume(")"))
        {
            node->update = expr();
            expect(")");
        }
        node->true_code = stmt();
        return node;
    }

    Node *node;
    if (consume_TokenKind(TK_RETURN))
    {
        node = calloc(1, sizeof(Node));
        node->kind = ND_RETURN;
        node->rhs = expr();
    }
    else
        node = expr();
    expect(";");
    return node;
}

Node *expr()
{
    pr_debug2("expr");
    return assign();
}

Node *assign()
{
    pr_debug2("assign");
    Node *node = equality();

    if (consume("="))
        node = new_node(ND_ASSIGN, node, assign());
    return node;
}

Node *equality()
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

Node *unary()
{
    pr_debug2("unary");
    if (consume("+"))
        return primary();
    if (consume("-"))
        return new_node(ND_SUB, new_node_num(0), primary());
    return primary();
}

Node *primary()
{
    pr_debug2("primary");
    if (consume("("))
    {
        Node *node = expr();
        expect(")");
        return node;
    }

    Token *token = consume_TokenKind(TK_IDENT);
    if (token)
    {
        Node *node = calloc(1, sizeof(Node));
        node->kind = ND_LVAR;
        LVar *lvar = find_lvar(token);
        if (lvar)
        {
            node->offset = lvar->offset;
        }
        else
        {
            lvar = calloc(1, sizeof(LVar));
            lvar->next = locals;
            lvar->name = token->str;
            lvar->len = token->len;
            lvar->offset = (locals ? locals->offset : 0) + 8;
            node->offset = lvar->offset;
            locals = lvar;
        }
        return node;
    }

    return new_node_num(expect_number());
}
