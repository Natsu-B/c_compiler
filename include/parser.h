#ifndef PARSER_C_COMPILER
#define PARSER_C_COMPILER

#include "tokenizer.h"

typedef struct Node Node;
typedef struct LVar LVar;
typedef struct GTLabel GTLabel;
typedef struct NDBlock NDBlock;
typedef struct FuncBlock FuncBlock;
typedef struct Type Type;

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
    GTLabel *next; // 次の変数
    char *name;    // 変数名 e.g. _0_main
    int len;       // 変数名の長さ
};

typedef enum
{
    ND_ADD,          // +
    ND_SUB,          // -
    ND_MUL,          // *
    ND_DIV,          // /
    ND_EQ,           // ==
    ND_NEQ,          // !=
    ND_LT,           // <
    ND_LTE,          // <=
    ND_ASSIGN,       // =
    ND_ADDR,         // &
    ND_DEREF,        // *
    ND_FUNCDEF,      // 関数定義
    ND_FUNCCALL,     // 関数呼び出し
    ND_RETURN,       // return
    ND_SIZEOF,       // sizeof
    ND_IF,           // if
    ND_ELIF,         // if else
    ND_FOR,          // for
    ND_WHILE,        // while
    ND_LVAR,         // ローカル変数
    ND_NUM,          // 整数
    ND_BLOCK,        // ブロック
    ND_DISCARD_EXPR, // 式文
    ND_END,          // デバッグ時利用
} NodeKind;

// デバッグ時利用 NodeKindに追加したら必ず追加すること
#define NodeKindTable "ND_ADD", "ND_SUB", "ND_MUL", "ND_DIV", "ND_EQ", "ND_NEQ", "ND_LT", "ND_LTE", "ND_ASSIGN", "ND_ADDR", "ND_DEREF", "ND_FUNCDEF", "ND_FUNCCALL", "ND_RETURN", "ND_SIZEOF", "ND_IF", "ND_ELIF", "ND_FOR", "ND_WHILE", "ND_LVAR", "ND_NUM", "ND_BLOCK", "ND_DISCARD_EXPR"
extern const char* nodekindlist[ND_END];

struct Node
{
    NodeKind kind; // ノードの種類
    Type *type;    // 型
    Token *token;  // パース元のトーケン

    // 以下は Nodeの種類によってそれぞれ1つしか使わないが、
    // あるNodeを起点にしてそれ以下のNodeを検索するプログラムを
    // 簡略化するため一時的にunionを使わないことにする
    struct
    {
        Node *lhs;     // 左辺 left-hand side
        Node *rhs;     // 右辺 right-hand side
        int real_size; // 置き換えられるサイズ sizeof演算子のみで用いる
    };

    struct
    {                    // if for while の場合
        GTLabel *name;   // ラベルの名前
        Node *condition; // 判定条件
        Node *true_code; // trueの際に実行されるコード
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
    struct
    {                    // ND_BLOCK ND_FUNCCALL ND_FUNCDEF
        NDBlock *expr;   // expr ND_FUNCCALL ND_FUNCDEFで利用
        NDBlock *stmt;   // stmt ND_BLOCK ND_FUNCDEFで利用
        char *func_name; // ND_FUNCCALL ND_FUNCDEF で利用 関数名
        int func_len;    // ND_FUNCCALL ND_FUNCDEF のときのみ利用 関数名長さ
    };
    long val;   // ND_NUMの場合 数値
    int offset; // ND_LVARの場合 RBP - offset の位置に変数がある
};

// 変数を管理するstruct
struct LVar
{
    LVar *next; // 次の変数
    char *name; // 変数名
    int len;    // 変数名 長さ
    int offset; // オフセット
    Type *type; // 型
};

// 引数、またはブロックの中の式を管理するstruct
struct NDBlock
{
    NDBlock *next; // 次の変数
    Node *node;    // ブロック内の式
};

// 関数の中の式を管理するstruct
struct FuncBlock
{
    FuncBlock *next; // 次の変数
    Node *node;      // 関数内の式
    int stacksize;   // スタックのサイズ byte単位
};

// サポートしている変数の型
typedef enum
{
    TYPE_INT, // TODO int型 unsigned 64bit
    TYPE_PTR, // 型へのポインタ
} TypeKind;

// 変数の型を管理するstruct
struct Type
{
    TypeKind type;
    Type *ptr_to;
};

Type *alloc_type(TypeKind kind);
FuncBlock *parser();

#endif // PARSER_C_COMPILER