#include "include/conditional_inclusion.h"

#include <string.h>

#include "include/define.h"
#include "include/error.h"
#include "include/preprocessor.h"
#include "include/vector.h"

// 引数のheadトークンから nextトークンまでのトークンを消す
// ただし、TK_LINEBREAKは残す
static void clean_while_next(Token *head, Token *next)
{
  while (head != next)
  {
    if (head->kind == TK_EOF) error_exit("#ifディレクティブが閉じていません");
    if (head->kind != TK_LINEBREAK) token_void(head);
    head = head->next;
  }
}

static void _conditional_inclusion(if_directive type, Token *head,
                                   Vector *conditional_list);

// #if #ifdef #ifndefに対応する #else #elif #endif 等を処理する関数
void next_conditional_inclusion(Token *token, bool is_true,
                                Vector *conditional_list, bool is_end)
{
  switch (token->len)
  {
    case 5:  // #else #elif
      if (!strncmp(token->str, "#else", 5))
      {  // #else
        token_void(token);
        Token *next = vector_shift(conditional_list);
        if (is_true) clean_while_next(token, next);
        next_conditional_inclusion(next, !is_true, conditional_list, true);
      }
      else
      {  // #elif
        unimplemented();
      }
      break;
    case 6:  // #endif
      token_void(token);
      return;
    case 8:  // #elifdef
    case 9:  // #elifndef
      if (is_end) error_at(token->str, token->len, "Invalid #elifdef use");
      if (!is_true)
      {
        if (token->len == 8)  // #elifdef
          _conditional_inclusion(token_ifdef, token, conditional_list);
        else  // #elifndef
          _conditional_inclusion(token_ifndef, token, conditional_list);
      }
      else
      {
        Token *next = vector_shift(conditional_list);
        clean_while_next(token, next);
        next_conditional_inclusion(next, is_true, conditional_list, false);
      }
      token_void(token);
      break;
    default:
      unreachable();
      break;
  }
}

static void _conditional_inclusion(if_directive type, Token *head,
                                   Vector *conditional_list)
{
  switch (type)
  {
    case token_if:
      unimplemented();
      break;
    case token_ifdef:
    case token_ifndef:
      token_void(head);
      do
      {
        head = head->next;
      } while (head->kind == TK_IGNORABLE);
      bool is_true = false;
      if (find_macro_name_all(head))
      {
        if (type == token_ifdef) is_true = true;
      }
      else if (type == token_ifndef)
        is_true = true;
      token_void(head);
      head = head->next;

      // #else #endif 等の対応するディレクティブ
      Token *next = vector_shift(conditional_list);
      if (!is_true) clean_while_next(head, next);
      next_conditional_inclusion(next, is_true, conditional_list, false);
      break;
    default:
      unreachable();
  }
}

void conditional_inclusion(if_directive type, Vector *conditional_list)
{
  Token *head = vector_shift(conditional_list);
  _conditional_inclusion(type, head, conditional_list);
}