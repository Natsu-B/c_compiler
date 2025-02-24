#ifndef TOKENIZER_C_COMPILER
#define TOKENIZER_C_COMPILER

#include <stdbool.h>

typedef enum
{
    TK_RESERVED, // 記号
    TK_IDENT,    // 識別子
    TK_FUNCDEF,  // 関数定義
    TK_FUNCCALL, // 関数呼び出し
    TK_NUM,      // 整数
    TK_RETURN,   // return
    TK_IF,       // if
    TK_ELSE,     // else
    TK_FOR,      // for
    TK_WHILE,    // while
    TK_EOF,      // 入力終了
    TK_END,      // デバッグ出力用
} TokenKind;     // トークンの種類

// デバッグ出力用 TokenKindに追加したら必ず追加すること
#define TokenKindTable "TK_RESERVED", "TK_IDENT", "TK_FUNCDEF", "TK_FUNCCALL", "TK_NUM", "TK_RETURN", "TK_IF", "TK_ELSE", "TK_FOR", "TK_WHILE", "TK_EOF"

typedef struct Token Token;

struct Token
{
    TokenKind kind; // トークンの種類
    Token *next;    // 次のトークン
    char *str;      // トークン文字列
    union           // トークンの種類に応じたデータを保存
    {
        long val; // 整数の場合の値
        int len;  // 識別子、関数、記号の場合 トークンの長さ
    };
};

extern void tokenizer(char *input);

extern bool at_eof();

extern bool peek_next_TokenKind(TokenKind kind);
extern bool peek_next(char *op);
extern Token *consume_TokenKind(TokenKind kind);
extern bool consume(char *op);
extern void expect(char *op);

extern long expect_number();

#endif // TOKENIZER_C_COMPILER