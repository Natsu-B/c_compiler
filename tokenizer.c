// ------------------------------------------------------------------------------------
// tokenizer
// ------------------------------------------------------------------------------------
#include "include/tokenizer.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "include/debug.h"
#include "include/error.h"
#include "include/preprocessor.h"
#include "include/vector.h"

Token *token;      // トークンの実体
Token *token_old;  // tokenの一つあとのトークン

const char *tokenkindlist[TK_END] = {TokenKindTable};

void fix_token_head()
{
  while (token->kind == TK_IGNORABLE || token->kind == TK_ILB ||
         token->kind == TK_LINEBREAK || token->kind == TK_EOF)
  {
    token = token->next;
    if (!token->next)
      break;
  }
}

// TK_IGNORABLE、 TK_LIB と TK_LINEBREAKを無視してトークンを進める
Token *token_next()
{
  do
  {
    if (token->kind != TK_IGNORABLE && token->kind != TK_ILB &&
        token->kind != TK_LINEBREAK)
      token_old = token;
    token = token->next;
  } while (token->kind == TK_IGNORABLE || token->kind == TK_ILB ||
           token->kind == TK_LINEBREAK);
  return token_old;
}

// トークンの位置を引数の位置に変更する
// かなり危険なため利用は注意を
void set_token(Token *next)
{
  token = next;
}

Token *get_token()
{
  return token;
}

// 次のトークンが引数のkindのトークンで、その次のトークンがTK_RESERVEDで
// 引数のreservedと等しければ消費する その他の場合NULLを返す
Token *consume_token_if_next_matches(TokenKind kind, char reserved)
{
  Token *next = token;
  do
  {
    next = next->next;
  } while (next->kind == TK_IGNORABLE || next->kind == TK_ILB ||
           next->kind == TK_LINEBREAK);
  if (token->kind == kind && next->kind == TK_RESERVED &&
      next->str[0] == reserved)
    return token_next();
  return NULL;
}

Token *peek(char *op, TokenKind kind)
{
  if (token->kind != kind || strlen(op) != token->len ||
      memcmp(op, token->str, token->len))
    return NULL;
  return token;
}

// 次のトークンが引数の記号だったら読み進めtokenをその他のときはNULLを返す関数
// ただし、TK_IGNORABLEを除く
Token *consume(char *op, TokenKind kind)
{
  if (peek(op, kind))
    return token_next();
  return NULL;
}

Token *consume_reserved(char *op)
{
  switch (strlen(op))
  {
    case 1: return consume(op, TK_RESERVED);
    case 2:
      if (token->kind == TK_RESERVED && token->len == 1 &&
          token->str[0] == op[0] && token->next->kind == TK_RESERVED &&
          token->next->len == 1 && token->next->str[0] == op[1])
      {
        token_next();
        return token_next();
      }
      return NULL;
    default: break;
  }
  unreachable();
  return NULL;
}

// 次のトークンが引数の記号だったら読み進め、そうでなければerror_atを呼び出す
Token *expect(char *op, TokenKind kind)
{
  Token *result = consume(op, kind);
  if (!result)
    error_at(token->str, token->len, "トークンが %s でありませんでした", op);
  return result;
}

// 一つ前のトークンを取得する
Token *get_old_token()
{
  return token_old;
}

Token *peek_ident()
{
  long tmp;
  if (token->kind != TK_IDENT || is_number(&tmp))
    return NULL;
  return token;
}

Token *consume_ident()
{
  if (peek_ident())
    return token_next();
  return NULL;
}

Token *expect_ident()
{
  Token *expect = consume_ident();
  if (!expect)
    error_at(token->str, token->len, "トークンがidentではありません");
  return expect;
}

Token *consume_string()
{
  if (token->kind != TK_STRING)
    return NULL;
  return token_next();
}

bool is_number(long *result)
{
  if (token->kind != TK_IDENT || !isdigit(token->str[0]))
    return false;
  char *ptr;
  *result = strtol(token->str, &ptr, 10);
  if (ptr - token->str != (int)token->len)
    return false;
  return true;
}

// 次のトークンが整数だった場合読み進め、それ以外だったらエラーを返す関数
long expect_number()
{
  long result;
  if (!is_number(&result))
    error_at(token->str, token->len, "トークンが整数ではありません");
  token_next();
  return result;
}

// トークンが最後(TK_EOF)だったらtrueを、でなければfalseを返す関数
bool at_eof()
{
  return token->kind == TK_EOF;
}

// 与えられた引数がトークンを構成するかどうか 英数字と '_'
int is_alnum(char c)
{
  return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') ||
         ('0' <= c && c <= '9') || (c == '_');
}

// 新しくトークンを作る関数
Token *new_token(TokenKind kind, char *str)
{
  Token *new = calloc(1, sizeof(Token));
  new->kind = kind;
  new->str = str;
  return new;
}

Token *tokenize_once(char *input, char **end)
{
  Token *cur;
  // 改行 isspaceでは'\n'も処理されてしまうのでそれより前に置く
  if (*input == '\n')
  {
    cur = new_token(TK_LINEBREAK, input++);
    cur->len = 1;
    *end = input;
    return cur;
  }

  size_t space_counter = 0;
  while (isspace(*input) && *input != '\n')
  {
    space_counter++;
    input++;
  }
  if (space_counter)
  {
    cur = new_token(TK_IGNORABLE, input - space_counter);
    cur->len = space_counter;
    *end = input;
    return cur;
  }

  // コメントを読み飛ばす
  if (!strncmp(input, "//", 2))
  {
    input++;
    size_t i = 2;
    while (*++input != '\n')
      i++;
    cur = new_token(TK_IGNORABLE, input - i);
    cur->len = i;
    *end = input;
    return cur;
  }
  if (!strncmp(input, "/*", 2))
  {
    char *comment_end = strstr(input + 2, "*/");
    if (!comment_end)
      error_at(input, 1, "コメントが閉じられていません");
    cur = new_token(TK_IGNORABLE, input);
    char *pointer = input;
    while (pointer <= comment_end)
    {
      if (*++pointer == '\n')
      {
        cur->len = pointer - input;
        cur = new_token(TK_LINEBREAK, pointer);
        cur->len = 1;
        cur = new_token(TK_IGNORABLE, pointer);
        input = pointer;
      }
    }
    cur->len = comment_end + 2 - input;
    *end = comment_end + 2;
    return cur;
  }

  if (*input == '#')
  {
    cur = new_token(TK_DIRECTIVE, input);
    cur->len = 1;
    *end = input + 1;
    return cur;
  }

  if (strchr("+-*/()=!<>;{},&[].\\'|%?:~^", *input))
  {
    if (*input == '\\' && *(input + 1) == '\n')
    {
      cur = new_token(TK_ILB, input);
      cur->len = 2;
      input += 2;
      *end = input;
      return cur;
    }
    cur = new_token(TK_RESERVED, input);
    pr_debug2("find RESERVED token: %.1s", input);
    cur->len = 1;
    *end = input + 1;
    return cur;
  }

  // 文字列の検知
  if (*input == '"')
  {
    int i = 0;  // 文字列のサイズ("は除く)
    while (*(++input) != '"')
      i++;
    input++;  // 終了まで送る
    cur = new_token(TK_STRING, input - i - 2);
    cur->len = i + 2;
    *end = input;
    return cur;
  }

  // 英数字と'_'のみの場合は変数または予約語とみなす
  int i = 0;
  while (is_alnum(*input))
  {
    i++;
    input++;
  }
  if (i)
  {
    cur = new_token(TK_IDENT, input - i);
    cur->len = i;
    *end = input;
    return cur;
  }
  error_at(input, 1, "トークナイズに失敗しました");
  return NULL;
}

// トークナイズする関数
Token *tokenizer(char *input, char *end, Token *next_token)
{
  pr_debug("start tokenizer...");
  Token head;
  head.next = NULL;
  Token *cur = &head;
  Vector *nest_list = vector_new();
  while (end ? input != end : *input)
  {
    char *end = NULL;
    cur->next = tokenize_once(input, &end);
    input = end;
    cur = cur->next;

    if (cur->kind == TK_DIRECTIVE)
    {
      size_t counter = 0;
      char *tmp = input;
      while (isspace(*tmp) && *tmp != '\n')
        tmp++;
      while (is_alnum(*tmp))
      {
        counter++;
        tmp++;
      }
      // Conditional_Inclusion (#if #endif 等)を高速化するためにまとめる
      if ((counter == 2 && !strncmp(tmp - counter, "if", 2)) ||
          (counter == 5 && !strncmp(tmp - counter, "ifdef", 5)) ||
          (counter == 6 && !strncmp(tmp - counter, "ifndef", 6)))
      {
        Vector *new = vector_new();
        vector_push(Conditional_Inclusion_List, new);
        vector_push(nest_list, new);
        vector_push(new, cur);
      }
      else if ((counter == 4 && (!strncmp(tmp - counter, "else", 4) ||
                                 !strncmp(tmp - counter, "elif", 4))) ||
               (counter == 7 && !strncmp(tmp - counter, "elifdef", 7)) ||
               (counter == 8 && !strncmp(tmp - counter, "elifndef", 8)))
        vector_push(vector_peek(nest_list), cur);
      else if (counter == 5 && !strncmp(tmp - counter, "endif", 5))
        vector_push(vector_pop(nest_list), cur);
    }
  }

  cur->next = new_token(TK_EOF, input);
  cur->next->next = next_token;

  pr_debug("complite tokenize");
  vector_free(nest_list);
#ifdef DEBUG
  print_tokenize_result(head.next);
#endif

  token = head.next;
  return token;
}