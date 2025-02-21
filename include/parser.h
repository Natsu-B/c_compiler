#ifndef PARSER_C_COMPILER
#define PARSER_C_COMPILER

#include "tokenizer.h"

typedef enum
{
    ND_ADD, // +
    ND_SUB, // -
    ND_MUL, // *
    ND_DIV, // /
    ND_EQ,  // ==
    ND_NEQ, // !=
    ND_LT,  // <
    ND_LTE, // <=
    ND_NUM, // 整数
} NodeKind;

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
        int val; // ND_NUMの場合 数値
    };
};

extern Node* parser();

#endif // PARSER_C_COMPILER