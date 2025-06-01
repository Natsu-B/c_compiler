#ifndef VARIABLES_C_COMPILER
#define VARIABLES_C_COMPILER

#include "parser.h"
#include "type.h"

// 変数を管理するstruct
struct Var
{
  Var *next;      // 次の変数
  char *name;     // 変数名
  size_t len;     // 変数名 長さ
  Type *type;     // 型
  Token *token;   // 変数に対応するトークン
  bool is_local;  // ローカル変数かグローバル変数か
  union
  {
    enum
    {                // global変数のとき 初期化をどうするか
      reserved,      // 0を使わないように
      init_zero,     // ゼロクリア
      init_val,      // 数字での初期値
      init_pointer,  // ポインタでの初期化
      init_string,   // 文字列での初期化
    } how2_init;
    size_t offset;  // ローカル変数の場合 RBP - offsetの位置に変数がある
  };
};

void init_variables();
NestedBlockVariables *new_nest_variables();
void exit_nest_variables();
Var *add_variables(Token *token, Type *type);
char *add_string_literal(Token *token);
Var *get_global_var();

#endif