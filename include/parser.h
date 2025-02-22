#ifndef PARSER_C_COMPILER
#define PARSER_C_COMPILER

#include "tokenizer.h"

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
typedef struct GTLabel GTLabel;

struct GTLabel
{
    GTLabel *next; // 次の変数
    char *name;    // 変数名 e.g. _0_main
    int len; //変数名の長さ
};

typedef enum
{
    ND_ADD,    // +
    ND_SUB,    // -
    ND_MUL,    // *
    ND_DIV,    // /
    ND_EQ,     // ==
    ND_NEQ,    // !=
    ND_LT,     // <
    ND_LTE,    // <=
    ND_ASSIGN, // =
    ND_RETURN, // return
    ND_IF,     // if
    ND_ELIF,   // if else
    ND_FOR,    // for
    ND_WHILE,  // while
    ND_LVAR,   // ローカル変数
    ND_NUM,    // 整数
    ND_END,    // デバッグ時利用
} NodeKind;

// デバッグ時利用 NodeKindに追加したら必ず追加すること
#define NodeKindTable "ND_ADD", "ND_SUB", "ND_MUL", "ND_DIV", "ND_EQ", "ND_NEQ", "ND_LT", "ND_LTE", "ND_ASSIGN", "ND_RETURN", "ND_IF", "ND_ELIF", "ND_FOR", "ND_WHILE", "ND_LVAR", "ND_NUM"

typedef struct Node Node;

struct Node
{
    NodeKind kind; // ノードの種類
    union
    {
        struct
        {
            Node *lhs; // 左辺 left-hand side
            Node *rhs; // 右辺 right-hand side
        };
        struct
        {                        // if for while の場合
            GTLabel *name; // ラベルの名前
            Node *condition;     // 判定条件
            Node *true_code;     // trueの際に実行されるコード
            union
            {
                Node *false_code; // if else文 falseの際に実行されるコード
                struct
                {                 // for文
                    Node *init;   // 初期化時のコード e.g. int i = 0
                    Node *update; // 毎ステップごとに実行されるコード e.g. i++
                };
            };
        };
        long val;   // ND_NUMの場合 数値
        int offset; // ND_LVARの場合 RBP - offset の位置に変数がある
    };
};

// 変数を管理するstruct
typedef struct LVar LVar;

struct LVar
{
    LVar *next; // 次の変数
    char *name; // 変数名
    int len;    // 長さ
    int offset; // オフセット
};

extern void parser();

#define max_num 100
extern Node *code[max_num];

#endif // PARSER_C_COMPILER