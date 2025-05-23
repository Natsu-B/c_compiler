#ifndef PARSER_C_COMPILER
#define PARSER_C_COMPILER

#include <stddef.h>

#include "tokenizer.h"
#include "type.h"

typedef struct Node Node;
typedef struct Var Var;
typedef struct GTLabel GTLabel;
typedef struct NDBlock NDBlock;
typedef struct FuncBlock FuncBlock;
typedef struct NestedBlockVariables NestedBlockVariables;

/**
 *  label の命名規則
 *
 *  if 文
 *  .LendXXX
 *  .LelseXXX
 *
 *  while 文
 *  .LbeginXXX
 *  .LendXXX
 *
 *  for 文
 *  .LbeginXXX
 *  .LendXXX
 *
 *  XXXには呼び出した関数名
 *  重複があれば0~の整数が入る e.g. begin_1_XXX end_1_XXX
 */

// goto labelを管理するstruct
struct GTLabel
{
  GTLabel *next;  // 次の変数
  char *name;     // 変数名 e.g. _0_main
  size_t len;     // 変数名の長さ
};

typedef enum
{
  ND_ADD,           // +
  ND_SUB,           // -
  ND_MUL,           // *
  ND_DIV,           // /
  ND_EQ,            // ==
  ND_NEQ,           // !=
  ND_LT,            // <
  ND_LTE,           // <=
  ND_ASSIGN,        // =
  ND_ADDR,          // &
  ND_DEREF,         // *
  ND_FUNCDEF,       // 関数定義
  ND_FUNCCALL,      // 関数呼び出し
  ND_RETURN,        // return
  ND_SIZEOF,        // sizeof
  ND_IF,            // if
  ND_ELIF,          // if else
  ND_FOR,           // for
  ND_WHILE,         // while
  ND_VAR,           // 変数
  ND_ARRAY,         // 配列
  ND_NUM,           // 整数
  ND_BLOCK,         // ブロック
  ND_DISCARD_EXPR,  // 式文
  ND_STRING,        // string literal
  ND_END,           // デバッグ時利用
} NodeKind;

// デバッグ時利用 NodeKindに追加したら必ず追加すること
#define NodeKindTable                                                        \
  "ND_ADD", "ND_SUB", "ND_MUL", "ND_DIV", "ND_EQ", "ND_NEQ", "ND_LT",        \
      "ND_LTE", "ND_ASSIGN", "ND_ADDR", "ND_DEREF", "ND_FUNCDEF",            \
      "ND_FUNCCALL", "ND_RETURN", "ND_SIZEOF", "ND_IF", "ND_ELIF", "ND_FOR", \
      "ND_WHILE", "ND_VAR", "ND_ARRAY", "ND_NUM", "ND_BLOCK",                \
      "ND_DISCARD_EXPR", "ND_STIRNG"
extern const char *nodekindlist[];

struct Node
{
  NodeKind kind;  // ノードの種類
  Type *type;     // 型
  Token *token;   // パース元のトーケン

  // 以下は Nodeの種類によってそれぞれ1つしか使わないが、
  // あるNodeを起点にしてそれ以下のNodeを検索するプログラムを
  // 簡略化するため一時的にunionを使わないことにする
  struct
  {
    Node *lhs;         // 左辺 left-hand side
    Node *rhs;         // 右辺 right-hand side
    size_t real_size;  // 置き換えられるサイズ sizeof演算子のみで用いる
  };

  struct
  {                                  // if for while の場合
    GTLabel *name;                   // ラベルの名前
    Node *condition;                 // 判定条件
    Node *true_code;                 // trueの際に実行されるコード
    NestedBlockVariables *nest_var;  // 一行のときも使う
    union
    {
      Node *false_code;  // if else文 falseの際に実行されるコード
      struct
      {                // for文
        Node *init;    // 初期化時のコード e.g. int i = 0
        Node *update;  // 毎ステップごとに実行されるコード e.g. i++
      };
    };
  };
  struct
  {                   // ND_BLOCK ND_FUNCCALL ND_FUNCDEF
    NDBlock *expr;    // expr ND_FUNCCALL ND_FUNCDEFで利用
    NDBlock *stmt;    // stmt ND_BLOCK ND_FUNCDEFで利用
    char *func_name;  // ND_FUNCCALL ND_FUNCDEF で利用 関数名
    size_t func_len;  // ND_FUNCCALL ND_FUNCDEF のときのみ利用 関数名長さ
    NestedBlockVariables
        *var_list;  // ND_FUNCDEF ND_BLOCK のとき利用 変数リスト
  };
  long val;  // ND_NUMの場合 数値
  struct
  {               // 変数(ND_VAR)の場合
    bool is_new;  // 初めて定義された変数か否か
    Var *var;     // 変数の情報
  };
  struct
  {                      // string型 ND_STRINGの場合
    char *literal_name;  // stirng literal にアクセスする名前
  };
};

// グローバル変数やローカル変数を調べるstruct
struct NestedBlockVariables
{
  NestedBlockVariables *next;  // 一つ前のネストを指す
  Var *var;                    // そのネスト内の変数
  size_t counter;              // 変数が同じネストにいくつあるか
};

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

// 引数、またはブロックの中の式を管理するstruct
struct NDBlock
{
  NDBlock *next;  // 次の変数
  Node *node;     // ブロック内の式
};

// 関数の中の式を管理するstruct
struct FuncBlock
{
  FuncBlock *next;   // 次の変数
  Node *node;        // 関数内の式
  size_t stacksize;  // スタックのサイズ byte単位
};

Node *new_node(NodeKind kind, Node *lhs, Node *rhs, Token *token);
FuncBlock *parser();

#endif  // PARSER_C_COMPILER