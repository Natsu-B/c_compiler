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
  TK_ILB,        // 無視できる改行 Ignorable Line Break
  TK_IDENT,      // 識別子
  TK_STRING,     // string literal
  TK_EOF,        // 入力終了
  TK_END,        // デバッグ出力用
} TokenKind;     // トークンの種類

// デバッグ出力用 TokenKindに追加したら必ず追加すること
#define TokenKindTable                                                     \
  "TK_RESERVED", "TK_DIRECTIVE", "TK_IGNORABLE", "TK_LINEBREAK", "TK_ILB", \
      "TK_IDENT", "TK_STRING", "TK_EOF"
extern const char *tokenkindlist[TK_END];

typedef struct Token Token;

struct Token
{
  TokenKind kind;  // トークンの種類
  Token *next;     // 次のトークン
  char *str;       // トークン文字列
  size_t len;      // トークンの長さ
};

Token *tokenize_once(char *input, char **end);
Token *tokenizer(char *input, char *end, Token *next_token);
void re_tokenize(Token *token_head);
bool at_eof();
void fix_token_head();
void set_token(Token *next);
Token *consume_token_if_next_matches(TokenKind kind, char reserved);
Token *peek(char *op, TokenKind kind);
Token *consume(char *op, TokenKind kind);
Token *expect(char *op, TokenKind kind);
Token *get_old_token();
Token *get_token();
Token *peek_ident();
Token *consume_ident();
Token *expect_ident();
Token *consume_string();
bool is_number(long *result);
long expect_number();

#endif  // TOKENIZER_C_COMPILER