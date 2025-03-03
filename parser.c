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

const char *nodekindlist[ND_END] = {NodeKindTable};

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
    size_t namesize = program_name_len + 6;
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

Node *new_node(NodeKind kind, Node *lhs, Node *rhs, Token *token)
{
    Node *node = calloc(1, sizeof(Node));
    node->kind = kind;
    node->lhs = lhs;
    node->rhs = rhs;
    node->token = token;

    return node;
}

Node *new_node_num(int val)
{
    Node *node = calloc(1, sizeof(Node));
    node->kind = ND_NUM;
    node->val = val;
    Type *type = calloc(1, sizeof(Type));
    type->type = TYPE_INT;
    node->type = type;
    return node;
}

// Typeを作成する関数
Type *alloc_type(TypeKind kind)
{
    Type *new = calloc(1, sizeof(Type));
    new->type = kind;
    return new;
}

/**
 * program    = type ( "*" )* ident "(" expr ("," expr )* ")"{stmt*}
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
 * unary      = "sizeof" unary
 *         | ("+" | "-")? postfix
 *         | "*" unary
 *         | "&" unary
 * postfix    = primary ( "[" expr "]" )*
 * primary    = num
 *         | ( type ( "*" )* )? ident
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
Node *postfix();
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
            i += 8;
            tmp = tmp->next;
        }
        new->stacksize = i;
    }
#ifdef DEBUG
    print_parse_result(head.next);
#endif
    pr_debug("complite parse");
    return head.next;
}

Node *program()
{
    pr_debug2("program");
    expect_tokenkind(TK_INT);
    Token *token = expect_tokenkind(TK_IDENT);
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
        if (!consume(")"))
        {
            NDBlock *next = calloc(1, sizeof(NDBlock));
            pointer->next = next;
            next->node = expr();
            pointer = next;
            while (!consume(")"))
            {
                expect(",");
                NDBlock *next = calloc(1, sizeof(NDBlock));
                pointer->next = next;
                next->node = expr();
                pointer = next;
            }
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
    return NULL; // definitely unreachable
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
            Node *new = calloc(1, sizeof(Node));
            node->init = new;
            new->kind = ND_DISCARD_EXPR;
            new->lhs = expr();
            expect(";");
        }
        if (!consume(";"))
        {
            node->condition = expr();
            expect(";");
        }
        if (!consume(")"))
        {
            Node *new = calloc(1, sizeof(Node));
            node->update = new;
            new->kind = ND_DISCARD_EXPR;
            new->lhs = expr();
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
    {
        node = new_node(ND_ASSIGN, node, assign(), get_old_token());
    }
    return node;
}

Node *equality()
{
    pr_debug2("equality");
    Node *node = relational();

    for (;;)
    {
        if (consume("=="))
            node = new_node(ND_EQ, node, relational(), get_old_token());
        else if (consume("!="))
            node = new_node(ND_NEQ, node, relational(), get_old_token());
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
            node = new_node(ND_LTE, node, add(), get_old_token());
        else if (consume("<"))
            node = new_node(ND_LT, node, add(), get_old_token());
        else if (consume(">="))
            node = new_node(ND_LTE, add(), node, get_old_token());
        else if (consume(">"))
            node = new_node(ND_LT, add(), node, get_old_token());
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
        {
            node = new_node(ND_ADD, node, mul(), get_old_token());
        }
        else if (consume("-"))
        {
            node = new_node(ND_SUB, node, mul(), get_old_token());
        }
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
            node = new_node(ND_MUL, node, unary(), get_old_token());
        else if (consume("/"))
            node = new_node(ND_DIV, node, unary(), get_old_token());
        else
            return node;
    }
}

Node *unary()
{
    pr_debug2("unary");
    if (consume_tokenkind(TK_SIZEOF))
        return new_node(ND_SIZEOF, unary(), NULL, get_old_token());
    if (consume("+"))
        return postfix();
    if (consume("-"))
        return new_node(ND_SUB, new_node_num(0), postfix(), get_old_token());
    if (consume("*"))
        return new_node(ND_DEREF, unary(), NULL, get_old_token());
    if (consume("&"))
        return new_node(ND_ADDR, unary(), NULL, get_old_token());
    return postfix();
}

Node *postfix()
{
    pr_debug2("postfix");
    Node *node = primary();
    for (;;)
    {
        if (consume("["))
        {
            node = new_node(ND_ARRAY, node, new_node_num(expect_number()), get_old_token());
            expect("]");
        }
        return node;
    }
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

    Token *token = consume_token_if_next_matches(TK_IDENT, '(');
    // 関数
    if (token)
    {
        expect("(");
        Node *node = calloc(1, sizeof(Node));
        node->token = token;
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
    int pointer_counter = 0;
    if (is_new)
    {
        while (consume("*"))
        {
            pointer_counter++;
        }
        token = expect_tokenkind(TK_IDENT);
    }
    else
        token = consume_tokenkind(TK_IDENT);
    // 変数
    if (token)
    {
        Node *node = calloc(1, sizeof(Node));
        node->token = token;
        node->kind = ND_LVAR;
        LVar *lvar = find_lvar(token);
        if (lvar)
        {
            if (is_new)
                error_at(token->str, "同じ名前の変数がすでにあります");
            node->offset = lvar->offset;
            node->type = lvar->type;
            while (pointer_counter--)
            {
                Node *new = calloc(1, sizeof(Node));
                new->kind = ND_DEREF;
                new->lhs = node;
                new->token = token;
                node = new;
            }
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
            // 型の指定
            Type *type = alloc_type(TYPE_INT);
            while (pointer_counter--)
            {
                Type *new = alloc_type(TYPE_PTR);
                new->ptr_to = type;
                type = new;
            }
            lvar->type = type;
            node->type = type;
            locals = lvar;
        }
        return node;
    }

    return new_node_num(expect_number());
}
