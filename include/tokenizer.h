#ifndef TOKENIZER_C_COMPILER
#define TOKENIZER_C_COMPILER

#ifdef SELF_HOST
#include "../test/compiler_header.h"
#else
#include <stdbool.h>
#include <stdlib.h>
#endif

typedef enum
{
  TK_RESERVED,   // Symbol
  TK_DIRECTIVE,  // Directive used in preprocessor
  TK_IGNORABLE,  // Ignorable elements like spaces, comments, etc.
  TK_LINEBREAK,  // Newline
  TK_ILB,        // Ignorable Line Break
  TK_IDENT,      // Ident
  TK_STRING,     // string literal
  TK_CHAR,       // character literal
  TK_EOF,        // End of input
  TK_END,        // For debug output
} TokenKind;     // Type of token

// For debug output. Must be added if TokenKind is added.
#define TokenKindTable                                                     \
  "TK_RESERVED", "TK_DIRECTIVE", "TK_IGNORABLE", "TK_LINEBREAK", "TK_ILB", \
      "TK_IDENT", "TK_STRING", "TK_CHAR", "TK_EOF"
extern const char *tokenkindlist[TK_END];

typedef struct Token Token;

struct Token
{
  TokenKind kind;  // Type of token
  Token *next;     // Next token
  char *str;       // Token string
  size_t len;      // Length of token
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
Token *get_token_head();
Token *get_token();
Token *peek_ident();
Token *consume_ident();
Token *expect_ident();
Token *consume_string();
Token *consume_char();
bool is_number(long long *result);
bool consume_number(long long *value);

#endif  // TOKENIZER_C_COMPILER