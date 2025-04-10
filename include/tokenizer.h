#ifndef TOKENIZER_C_COMPILER
#define TOKENIZER_C_COMPILER

#include <stdbool.h>
#include <stdlib.h>

typedef enum
{
    TK_RESERVED,  // 記号
    TK_DIRECTIVE, // preprocessorで使われるディレクティブ
    TK_IGNORABLE, // スペース、コメントアウト等の無視できるものたち
    TK_LINEBREAK, // 改行
    TK_IDENT,     // 識別子
    TK_NUM,       // 整数
    TK_RETURN,    // return
    TK_SIZEOF,    // sizeof
    TK_INT,       // int 32bit
    TK_CHAR,      // char 8bit
    TK_LONG,      // long 64bit
    TK_IF,        // if
    TK_ELSE,      // else
    TK_FOR,       // for
    TK_WHILE,     // while
    TK_STRING,    // string literal
    TK_EOF,       // 入力終了
    TK_END,       // デバッグ出力用
} TokenKind;      // トークンの種類

// デバッグ出力用 TokenKindに追加したら必ず追加すること
#define TokenKindTable "TK_RESERVED", "TK_DIRECTIVE", "TK_IGNORABLE", "TK_LINEBREAK", "TK_IDENT", "TK_NUM", "TK_RETURN", "TK_SIZEOF", "TK_INT", "TK_CHAR", "TK_LONG", "TK_IF", "TK_ELSE", "TK_FOR", "TK_WHILE", "TK_STRING", "TK_EOF"
extern const char *tokenkindlist[TK_END];

typedef struct Token Token;

struct Token
{
    TokenKind kind; // トークンの種類
    Token *next;    // 次のトークン
    char *str;      // トークン文字列
    union           // トークンの種類に応じたデータを保存
    {
        long val;   // 整数の場合の値
        size_t len; // 識別子、関数、記号の場合 トークンの長さ
    };
};

extern Token *tokenizer(char *input, Token *next_token);

extern bool at_eof();

extern void set_token(Token *next);
extern bool peek_next_TokenKind(TokenKind kind);
extern bool peek_next(char *op);
extern Token *consume_token_if_next_matches(TokenKind kind, char reserved);
extern Token *expect_tokenkind(TokenKind kind);
extern Token *consume_tokenkind(TokenKind kind);
extern Token *consume(char *op);
extern Token *expect(char *op);
extern Token *get_old_token();
extern Token *get_token();

extern long expect_number();

#endif // TOKENIZER_C_COMPILER