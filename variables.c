// ------------------------------------------------------------------------------------
// global variable manager
// ------------------------------------------------------------------------------------

#include "include/variables.h"
#include "include/parser.h"
#include "include/error.h"
#include <stdlib.h>
#include <string.h>

NestedBlockVariables *top;
NestedBlockVariables *root;
Var *locals;
size_t var_counter;

Var *find_local_var_in_current_nested_block(Token *token)
{
    for (Var *var = locals; var; var = var->next)
        if (var->len == token->len && !memcmp(var->name, token->str, var->len))
            return var;
    return NULL;
}

// 今まで出てきたローカル変数をすべて探索する ネストが深い方から探索していく
Var *find_local_var_all(Token *token)
{
    if (top == root)
        return NULL;
    Var *tmp = find_local_var_in_current_nested_block(token);
    if (tmp)
        return tmp;
    for (NestedBlockVariables *pointer = top->next; pointer != root; pointer = pointer->next)
        for (Var *var = pointer->var; var; var = var->next)
            if (var->len == token->len && !memcmp(var->name, token->str, var->len))
                return var;
    return NULL;
}

// グローバル変数を探索する
Var *find_global_var(Token *token)
{
    for (Var *var = root->var; var; var = var->next)
        if (var->len == token->len && !memcmp(var->name, token->str, token->len))
            return var;
    return NULL;
}

// 最初に1度のみ実行する関数
void init_variables()
{
    NestedBlockVariables *tmp = calloc(1, sizeof(NestedBlockVariables));
    root = tmp;
    top = tmp;
}

// ネストを1段深くするときに実行する関数
NestedBlockVariables *new_nest()
{
    NestedBlockVariables *tmp = calloc(1, sizeof(NestedBlockVariables));
    tmp->next = top;
    top->var = locals;
    top = tmp;
    locals = top->var;
    var_counter = top->counter;
    return top;
}

// ネストを戻すときに実行する関数
void exit_nest()
{
    if (!top->next)
        unreachable(); // parserでエラーになるはず
    top = top->next;
    locals = top->var;
    var_counter = top->counter;
}

// 変数の追加
Var *add_variables(Token *token, TypeKind kind, size_t pointer_counter)
{
    // 変数名が同じものが以前あったかどうかを調査
    Var *same = find_local_var_in_current_nested_block(token);
    Var *all_locals = find_local_var_all(token);
    Var *globals = find_global_var(token);
    Var *all = all_locals ? all_locals : globals;
    if (same && kind != TYPE_NULL)
        error_at(token->str, "同じ名前の変数がすでに存在します");
    if (!all && kind == TYPE_NULL)
        error_at(token->str, "この名前の変数は存在しません");
    if (all && kind == TYPE_NULL)
        return all; // 以前の値を返す

    // 新規変数の場合
    Var *new = calloc(1, sizeof(Var));
    new->next = locals;
    new->name = token->str;
    new->len = token->len;
    Type *type = alloc_type(kind);
    while (pointer_counter--)
    {
        Type *ref = alloc_type(TYPE_PTR);
        ref->ptr_to = type;
        type = ref;
    }
    new->type = type;
    new->counter = var_counter++;
    if (root == top)
        new->is_local = false; // 変数はグローバル変数
    else
        new->is_local = true; // 変数はローカル変数
    locals = new;
    return new;
}

Token* tmp;
char *add_string_literal(Token *token)
{
    tmp = token;
    return NULL;
}