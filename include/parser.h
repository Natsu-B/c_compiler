#ifndef PARSER_C_COMPILER
#define PARSER_C_COMPILER

#ifdef SELF_HOST
#include "../test/compiler_header.h"
#else
#include <stddef.h>
#endif

#include "tokenizer.h"
#include "type.h"
#include "vector.h"

typedef struct Node Node;
typedef struct Var Var;
typedef struct GTLabel GTLabel;
typedef struct NDBlock NDBlock;
typedef struct FuncBlock FuncBlock;
typedef struct NestedBlockVariables NestedBlockVariables;

typedef enum
{
  ND_ADD,            // +
  ND_SUB,            // -
  ND_MUL,            // *
  ND_DIV,            // /
  ND_IDIV,           // %
  ND_EQ,             // ==
  ND_NEQ,            // !=
  ND_LT,             // <
  ND_LTE,            // <=
  ND_ASSIGN,         // =
  ND_ADDR,           // &
  ND_DEREF,          // *
  ND_PREINCREMENT,   // ++ ident
  ND_PREDECREMENT,   // -- ident
  ND_POSTINCREMENT,  // ident ++
  ND_POSTDECREMENT,  // ident --
  ND_FUNCDEF,        // 関数定義
  ND_FUNCCALL,       // 関数呼び出し
  ND_RETURN,         // return
  ND_SIZEOF,         // sizeof
  ND_IF,             // if
  ND_ELIF,           // if else
  ND_FOR,            // for
  ND_WHILE,          // while
  ND_DO,             // do while
  ND_TERNARY,        // : ? 三項演算子
  ND_LOGICAL_OR,     // ||
  ND_LOGICAL_AND,    // &&
  ND_INCLUSIVE_OR,   // |
  ND_EXCLUSIVE_OR,   // ^
  ND_AND,            // &
  ND_LEFT_SHIFT,     // <<
  ND_RIGHT_SHIFT,    // >>
  ND_ASSIGNMENT,     // += -=等
  ND_VAR,            // 変数
  ND_ARRAY,          // 配列
  ND_DOT,            // .
  ND_ARROW,          // ->
  ND_FIELD,          // structのchild
  ND_NUM,            // 整数
  ND_BLOCK,          // ブロック
  ND_DISCARD_EXPR,   // 式文
  ND_STRING,         // string literal
  ND_GOTO,           // goto, continue, break
  ND_LABEL,          // goto label
  ND_CASE,           // case
  ND_SWITCH,         // switch
  ND_END,            // デバッグ時利用
} NodeKind;

/**
 *  label の命名規則
 *
 *  if 文
 *  .Lend_N_XXX
 *  .Lelse_N_XXX
 *
 *  while 文
 *  .Lbegin_N_XXX
 *  .Lend_N_XXX
 *
 *  for 文
 *  .Lbegin_N_XXX
 *  .Lend_N_XXX
 *
 *  goto 文のとき
 *  .Lgoto_YYY_XXX
 *
 *  XXXには呼び出した関数名 YYYにはラベル名
 *  Nには 0~の整数が入る e.g. begin_1_XXX end_1_XXX
 */
// goto labelを管理するstruct
struct GTLabel
{
  NodeKind kind;  // Nodeの名前
  char *name;     // 変数名 e.g. _0_main
  size_t len;     // 変数名の長さ
};

// デバッグ時利用 NodeKindに追加したら必ず追加すること
#define NodeKindTable                                                          \
  "ND_ADD", "ND_SUB", "ND_MUL", "ND_DIV", "ND_IDIV", "ND_EQ", "ND_NEQ",        \
      "ND_LT", "ND_LTE", "ND_ASSIGN", "ND_ADDR", "ND_DEREF",                   \
      "ND_PREINCREMENT", "ND_PREDECREMENT", "ND_POSTINCREMENT",                \
      "ND_POSTDECREMENT", "ND_FUNCDEF", "ND_FUNCCALL", "ND_RETURN",            \
      "ND_SIZEOF", "ND_IF", "ND_ELIF", "ND_FOR", "ND_WHILE", "ND_DO",          \
      "ND_TERNARY", "ND_LOGICAL_OR", "ND_LOGICAL_AND", "ND_INCLUSIVE_OR",      \
      "ND_EXCLUSIVE_OR", "ND_AND", "ND_LEFT_SHIFT", "ND_RIGHT_SHIFT",          \
      "ND_ASSIGNMENT", "ND_VAR", "ND_ARRAY", "ND_DOT", "ND_ARROW", "ND_FIELD", \
      "ND_NUM", "ND_BLOCK", "ND_DISCARD_EXPR", "ND_STIRNG", "ND_GOTO",         \
      "ND_LABEL", "ND_CASE", "ND_SWITCH"
extern const char *nodekindlist[ND_END];

struct Node
{
  NodeKind kind;  // ノードの種類
  Type *type;     // 型
  Token *token;   // パース元のトーケン

  // 以下は Nodeの種類によってそれぞれ1つしか使わないが、
  // あるNodeを起点にしてそれ以下のNodeを検索するプログラムを
  // 簡略化するため一時的にunionを使わないことにする
  // struct
  // {
  Node *lhs;            // 左辺 left-hand side
  Node *rhs;            // 右辺 right-hand side
  Node *chs;            // 三項演算子のときのみ利用
  size_t child_offset;  // ND_DOT, ND_ARROWのとき利用 そのchildのoffset
                        // };

  // struct
  // {                                  // if for while switch の場合
  GTLabel *name;                   // ラベルの名前 goto でも利用
  Node *condition;                 // 判定条件
  Node *true_code;                 // trueの際に実行されるコード
  NestedBlockVariables *nest_var;  // 一行のときも使う
  Node *false_code;                // if else文 falseの際に実行されるコード
                                   // struct
                                   // {                // for文
  Node *init;                      // 初期化時のコード e.g. int i = 0
  Node *update;                    // 毎ステップごとに実行されるコード e.g. i++
  // };
  Vector *case_list;  // switch文 case の Node*が入っている
                      // };
                      // struct
                      // {                   // ND_BLOCK ND_FUNCCALL ND_FUNCDEF
  Vector *expr;       // expr ND_FUNCCALL ND_FUNCDEFで利用
  NDBlock *stmt;      // stmt ND_BLOCK ND_FUNCDEFで利用
  char *func_name;    // ND_FUNCCALL ND_FUNCDEF で利用 関数名
  size_t func_len;    // ND_FUNCCALL ND_FUNCDEF のときのみ利用 関数名長さ
  // };
  long val;     // ND_NUMの場合数値 ND_POST/PRE INCREMENT/DECREMENT の場合
                // その型がptrのときその大きさ、数値のとき0
                // struct
                // {               // 変数(ND_VAR)の場合
  bool is_new;  // 初めて定義された変数か否か
  Var *var;     // 変数の情報
                // };
                // struct
                // {                      // string型 ND_STRINGの場合
  char *literal_name;  // stirng literal にアクセスする名前
                       // };
                       // struct
  // {                            // ND_GOTO ND_LABEL ND_CASE ND_DEFAULTの場合
  Node *statement_child;     // statement
  char *label_name;          // ラベルの名前 ND_CASE ND_DEFAULTでは使われない
  bool is_case;              // 以下ND_CASE ND_DEFAULT の場合
  size_t case_num;           // switch文の中で何番目のcaseか 0始まり
  GTLabel *switch_name;      // switch文のlabelの名前
  long constant_expression;  // case n: のn部分
  // };
};

// グローバル変数やローカル変数を調べるstruct
struct NestedBlockVariables
{
  NestedBlockVariables *next;  // 一つ前のネストを指す
  Var *var;                    // そのネスト内の変数
  size_t counter;              // 変数が同じネストにいくつあるか
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
Node *constant_expression();
FuncBlock *parser();

#endif  // PARSER_C_COMPILER