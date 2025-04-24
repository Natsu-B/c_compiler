#ifndef TOKENIZER_C_COMPILER
#define TOKENIZER_C_COMPILER

#include <stdbool.h>
#include <stdlib.h>

typedef enum
{
  TK_RESERVED,   // 記号
  TK_DIRECTIVE,  // preprocessorで使われるディレクティブ
  TK_IGNORABLE,  // スペース、コメントアウト等の無視できるものたち
  TK_LINEBREAK,  // 改行
  TK_IDENT,      // 識別子
  TK_NUM,        // 整数
  TK_STRING,     // string literal
  TK_EOF,        // 入力終了
  TK_END,        // デバッグ出力用
} TokenKind;     // トークンの種類

// デバッグ出力用 TokenKindに追加したら必ず追加すること
#define TokenKindTable                                                       \
  "TK_RESERVED", "TK_DIRECTIVE", "TK_IGNORABLE", "TK_LINEBREAK", "TK_IDENT", \
      "TK_NUM", "TK_STRING", "TK_EOF"
extern const char *tokenkindlist[TK_END];

typedef struct Token Token;

struct Token
{
  TokenKind kind;  // トークンの種類
  Token *next;     // 次のトークン
  char *str;       // トークン文字列
  size_t len;      // トークンの長さ
};

extern Token *tokenizer(char *input, Token *next_token);
extern void re_tokenize(Token *token_head);
extern bool at_eof();

extern void fix_token_head();
extern void set_token(Token *next);
extern Token *consume_token_if_next_matches(TokenKind kind, char reserved);
extern Token *consume(char *op, TokenKind kind);
extern Token *expect(char *op, TokenKind kind);
extern Token *get_old_token();
extern Token *get_token();
extern Token *consume_ident();
extern Token *expect_ident();
extern Token *consume_string();
extern bool is_number(long *result);
extern long expect_number();

#endif  // TOKENIZER_C_COMPILER