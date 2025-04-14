// ------------------------------------------------------------------------------------
// global variable manager
// ------------------------------------------------------------------------------------

#include "include/variables.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "include/error.h"
#include "include/parser.h"
#include "include/vector.h"

static NestedBlockVariables *top;   // 現在のネストの変数
static NestedBlockVariables *root;  // グローバル変数
static Vector *variables_list;      // variables_headの配列
static Var *globals_head;           // グローバル変数の先頭を指す
static Var *variables_head;         // 現在のネストの変数の先頭を指す
static size_t var_counter;

static Var *find_local_var_in_current_nested_block(Token *token)
{
  for (Var *var = top->var; var; var = var->next)
    if (var->len == token->len && !strncmp(var->name, token->str, var->len))
      return var;
  return NULL;
}

// 今まで出てきたローカル変数をすべて探索する ネストが深い方から探索していく
static Var *find_local_var_all(Token *token)
{
  if (top == root) return NULL;
  for (NestedBlockVariables *pointer = top; pointer != root;
       pointer = pointer->next)
    for (Var *var = pointer->var; var; var = var->next)
      if (var->len == token->len && !strncmp(var->name, token->str, var->len))
        return var;
  return NULL;
}

static Var *find_global_var_no_token(char *var_name, size_t var_len)
{
  for (Var *var = root->var; var; var = var->next)
    if (var->len == var_len && !strncmp(var->name, var_name, var_len))
      return var;
  return NULL;
}

// グローバル変数を探索する
static Var *find_global_var(Token *token)
{
  return find_global_var_no_token(token->str, token->len);
}

// 最初に1度のみ実行する関数
void init_variables()
{
  NestedBlockVariables *tmp = calloc(1, sizeof(NestedBlockVariables));
  root = tmp;
  top = tmp;
  variables_list = vector_new();
}

// ネストを1段深くするときに実行する関数
NestedBlockVariables *new_nest()
{
  NestedBlockVariables *tmp = calloc(1, sizeof(NestedBlockVariables));
  tmp->next = top;
  vector_push(variables_list, variables_head);
  variables_head = NULL;
  top = tmp;
  var_counter = top->counter;
  return top;
}

// ネストを戻すときに実行する関数
void exit_nest()
{
  if (!top->next) unreachable();  // parserでエラーになるはず
  top = top->next;
  variables_head = vector_pop(variables_list);
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
    error_at(token->str, token->len, "同じ名前の変数がすでに存在します");
  if (!all && kind == TYPE_NULL)
    error_at(token->str, token->len, "この名前の変数は存在しません");
  if (all &&
      kind ==
          TYPE_NULL)  // 以前から変数が存在し、変数の新規宣言をしていない場合
    return all;       // 以前の値を返す

  // 新規変数の場合
  Var *new = calloc(1, sizeof(Var));
  new->token = token;
  if (variables_head) variables_head->next = new;
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
  if (root == top)
  {
    new->is_local = false;  // 変数はグローバル変数
    globals_head = new;
  }
  else

    new->is_local = true;  // 変数はローカル変数
  if (!top->var)           // topにまだ変数がない場合
    top->var = new;
  variables_head = new;
  return new;
}

typedef struct literal_list literal_list;

struct literal_list
{
  literal_list *next;
  char *literal_name;
  char *name;
  size_t len;
};

static literal_list *literal_top;
static size_t literal_counter;

// ポインタへの文字列リテラルの代入の際利用
char *add_string_literal(Token *token)
{
  pr_debug2("string literal found");
  // 同じ文字列がすでに存在した場合、それを使う
  // 仕様として文字列の変更が認められていないため可能
  for (literal_list *pointer = literal_top; pointer; pointer = pointer->next)
    if (token->len == pointer->len &&
        !strncmp(token->str, pointer->name, pointer->len))
      return pointer->literal_name;
  literal_list *new = calloc(1, sizeof(literal_list));
  if (literal_top) literal_top->next = new;
  literal_top = new;
  literal_top->name = token->str;
  literal_top->len = token->len;
  // 文字列リテラルの名前を決める
  // 9999個まで文字列リテラルが存在可能
  char *literal_name = malloc(8);
  int snprintf_return = snprintf(literal_name, 8, ".LC%lu", literal_counter++);
  if (snprintf_return < 3 || snprintf_return > 7) unreachable();
  pr_debug2("literal_name: %s", literal_name);
  literal_top->literal_name = literal_name;
  // グローバル変数に追加
  if (find_global_var_no_token(literal_name, snprintf_return)) unreachable();
  Var *new_var = calloc(1, sizeof(Var));
  if (globals_head)
    globals_head->next = new_var;
  else
    root->var = new_var;
  new_var->token = token;
  new_var->name = literal_name;
  new_var->len = snprintf_return;
  new_var->type = alloc_type(TYPE_STR);
  new_var->is_local = false;
  new_var->how2_init = init_string;
  globals_head = new_var;
  return literal_name;
}

Var *get_global_var() { return root->var; }