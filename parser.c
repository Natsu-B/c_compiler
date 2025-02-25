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
// 現在パースしている関数名と名前
char *program_name;
int program_name_len;

GTLabel *generate_label_name()
{
    size_t namesize = program_name_len + 4;
    GTLabel *next = calloc(1, sizeof(GTLabel));
    next->len = namesize;
    next->next = head_label;
    char *mangle_name = malloc(namesize);
    int i = 0;
    for (;;)
    {
        snprintf(mangle_name, namesize, "_%d_%.*s", i++, program_name_len, program_name);
        GTLabel *var = head_label;
        for (;;)
        {
            // 全部探索し終えたら
            if (!var)
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
 * program    = type ident "(" expr ("," expr )* ")"{stmt*}
 * stmt    = expr ";"
 *         | "{" stmt "}"
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
 *         | "*" unary
 *         | "&" unary
 * primary    = num
 *         | ident
 *         | ident "(" expr ("," expr )* ")"
 *         | "(" expr ")"
 */

Node *program();
Node *stmt();
Node *expr();
Node *assign();
Node *equality();
Node *relational();
Node *add();
Node *mul();
Node *unary();
Node *primary();

FuncBlock *parser()
{
    pr_debug("start parser...");
    FuncBlock head;
    head.next = NULL;
    FuncBlock *pointer = &head;
    while (!at_eof())
    {
        locals = NULL;
        FuncBlock *new = calloc(1, sizeof(FuncBlock));
        pointer->next = new;
        new->node = program();
        pointer = new;
        int i = 0;
        LVar *tmp = locals;
        while (tmp)
        {
            i++;
            tmp = tmp->next;
        }
        new->stacksize = i;
    }
#ifdef DEBUG
    char *NodeKindList[ND_END] = {NodeKindTable};
    print_parse_result(head.next, NodeKindList);
#endif
    pr_debug("complite parse");
    return head.next;
}

Node *program()
{
    pr_debug2("program");
    expect_tokenkind(TK_INT);
    Token *token = expect_tokenkind(TK_FUNCDEF);
    if (token)
    {
        Node *node = calloc(1, sizeof(Node));
        node->kind = ND_FUNCDEF;
        node->func_name = token->str;
        node->func_len = token->len;
        program_name = token->str;
        program_name_len = token->len;
        NDBlock head;
        head.next = NULL;
        NDBlock *pointer = &head;
        expect("(");
        while (!consume(")"))
        {
            NDBlock *next = calloc(1, sizeof(NDBlock));
            pointer->next = next;
            next->node = expr();
            pointer = next;
            consume(","); // TODO 今の所 ",)" が許容されてしまう
        }

        node->expr = head.next;
        head.next = NULL;
        pointer = &head;
        expect("{");
        while (!consume("}"))
        {
            NDBlock *next = calloc(1, sizeof(NDBlock));
            pointer->next = next;
            next->node = stmt();
            pointer = next;
        }
        node->stmt = head.next;
        return node;
    }
    error_exit("unreachable");
    return NULL; // unreachable
}

Node *stmt()
{
    pr_debug2("stmt");
    // ブロックの判定
    if (consume("{"))
    {
        Node *node = calloc(1, sizeof(Node));
        NDBlock head;
        head.next = NULL;
        NDBlock *pointer = &head;
        node->kind = ND_BLOCK;
        for (;;)
        {
            if (consume("}"))
                break;
            NDBlock *next = calloc(1, sizeof(NDBlock));
            pointer->next = next;
            next->node = stmt();
            pointer = next;
        }
        node->stmt = head.next;
        return node;
    }

    // if文の判定とelseがついてるかどうか
    if (consume_tokenkind(TK_IF))
    {
        Node *node = calloc(1, sizeof(Node));
        node->name = generate_label_name();
        expect("(");
        node->condition = expr();
        expect(")");
        node->true_code = stmt();
        if (consume_tokenkind(TK_ELSE))
        {
            node->kind = ND_ELIF;
            node->false_code = stmt();
        }
        else
        {
            node->kind = ND_IF;
        }
        return node;
    }

    // while文の判定
    if (consume_tokenkind(TK_WHILE))
    {
        Node *node = calloc(1, sizeof(Node));
        node->kind = ND_WHILE;
        node->name = generate_label_name();
        expect("(");
        node->condition = expr();
        expect(")");
        node->true_code = stmt();
        return node;
    }

    // for文の判定
    if (consume_tokenkind(TK_FOR))
    {
        Node *node = calloc(1, sizeof(Node));
        node->kind = ND_FOR;
        node->name = generate_label_name();
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
    if (consume_tokenkind(TK_RETURN))
    {
        node = calloc(1, sizeof(Node));
        node->kind = ND_RETURN;
        node->rhs = expr();
    }
    else
    {
        node = calloc(1, sizeof(Node));
        node->kind = ND_DISCARD_EXPR;
        node->lhs = expr();
    }
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
    if (consume("*"))
        return new_node(ND_DEREF, unary(), NULL);
    if (consume("&"))
        return new_node(ND_ADDR, unary(), NULL);
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

    Token *token = consume_tokenkind(TK_FUNCCALL);
    // 関数
    if (token)
    {
        expect("(");
        Node *node = calloc(1, sizeof(Node));
        NDBlock head;
        head.next = NULL;
        NDBlock *pointer = &head;
        node->kind = ND_FUNCCALL;
        node->func_name = token->str;
        node->func_len = token->len;
        while (!consume(")"))
        {
            NDBlock *next = calloc(1, sizeof(NDBlock));
            pointer->next = next;
            next->node = expr();
            pointer = next;
            if (!consume(","))
            {
                expect(")");
                break;
            }
        }
        node->expr = head.next;
        return node;
    }

    // 変数の型
    Token *is_new = consume_tokenkind(TK_INT);
    token = consume_tokenkind(TK_IDENT);
    // 変数
    if (token)
    {
        Node *node = calloc(1, sizeof(Node));
        node->kind = ND_LVAR;
        LVar *lvar = find_lvar(token);
        if (lvar)
        {
            if (is_new)
                error_at(token->str, "同じ名前の変数がすでにあります");
            node->offset = lvar->offset;
        }
        else
        {
            if (!is_new)
                error_at(token->str, "型が指定されていません");
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
