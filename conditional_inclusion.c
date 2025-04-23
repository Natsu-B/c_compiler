#include "include/conditional_inclusion.h"

#include <ctype.h>
#include <string.h>

#include "include/debug.h"
#include "include/define.h"
#include "include/error.h"
#include "include/preprocessor.h"
#include "include/vector.h"

static void clean_while_next(Token *head, Token *next);

// 右結合の優先順位を表す
// 小さい方が優先順位が高い
static size_t operator_precedence(conditional_inclusion_type type)
{
  switch (type)
  {
    case CPPTK_Conditional:  // ?:
      return 0;
    case CPPTK_Logical_OR:  // ||
      return 1;
    case CPPTK_Logical_AND:  // &&
      return 2;
    case CPPTK_Inclusive_OR:  // |
      return 3;
    case CPPTK_Exclusive_OR:  // ^
      return 4;
    case CPPTK_AND:  // &
      return 5;
    case CPPTK_Equality:   // ==
    case CPPTK_NEquality:  // !=
      return 6;
    case CPPTK_LessThan:       // <
    case CPPTK_LessThanEq:     // <=
    case CPPTK_GreaterThan:    // >
    case CPPTK_GreaterThanEq:  // >=
      return 7;
    case CPPTK_LeftShift:   // <<
    case CPPTK_RightShift:  // >>
      return 8;
    case CPPTK_Plus:   // +
    case CPPTK_Minus:  // -
      return 9;
    case CPPTK_Mul:          // *
    case CPPTK_Div:          // /
    case CPPTK_DivReminder:  // %
      return 10;
    case CPPTK_UnaryPlus:   // +
    case CPPTK_UnaryMinus:  // -
    case CPPTK_NOT:         // !
    case CPPTK_Bitwise:     // ~
      return 11;
    default: unreachable(); return __UINT32_MAX__;  // unreachable
  }
}

conditional_inclusion_type reserved_token_to_type(Token *token, bool is_unary)
{
  if (token->kind != TK_RESERVED)
    unreachable();
  if (is_unary)
  {
    if (token->len != 1)
      error_at(token->str, token->len, "invalid #if directive");
    switch (token->str[0])
    {
      case '+': return CPPTK_UnaryPlus;
      case '-': return CPPTK_UnaryMinus;
      case '!': return CPPTK_NOT;
      case '~': return CPPTK_Bitwise;
      case '(': return CPPTK_Parentheses_Start;
      default: break;
    }
  }
  else
  {
    switch (token->len)
    {
      case 1:
        switch (token->str[0])
        {
          case '<': return CPPTK_LessThan;
          case '>': return CPPTK_GreaterThan;
          case '+': return CPPTK_Plus;
          case '-': return CPPTK_Minus;
          case '*': return CPPTK_Mul;
          case '/': return CPPTK_Div;
          case '%': return CPPTK_DivReminder;
          case '&': return CPPTK_AND;
          case '|': return CPPTK_Inclusive_OR;
          case '^': return CPPTK_Exclusive_OR;
          case ')': return CPPTK_Parentheses_End;
          default: break;
        }
        break;

      case 2:
        if (memcmp(token->str, "&&", 2) == 0)
          return CPPTK_Logical_AND;
        if (memcmp(token->str, "||", 2) == 0)
          return CPPTK_Logical_OR;
        if (memcmp(token->str, "==", 2) == 0)
          return CPPTK_Equality;
        if (memcmp(token->str, "!=", 2) == 0)
          return CPPTK_NEquality;
        if (memcmp(token->str, "<=", 2) == 0)
          return CPPTK_LessThanEq;
        if (memcmp(token->str, ">=", 2) == 0)
          return CPPTK_GreaterThanEq;
        if (memcmp(token->str, "<<", 2) == 0)
          return CPPTK_LeftShift;
        if (memcmp(token->str, ">>", 2) == 0)
          return CPPTK_RightShift;
        break;
      default: break;
    }
  }
  error_at(token->str, token->len, "invalid #if directive");
  return CPPTK_Reserved;  // unreachable
}

Vector *output_list;

static void shunting_yard_algorithm(Token *token)
{
  Token *head = token;
  output_list = vector_new();
  Vector *operator_stack = vector_new();
  bool is_unary = true;
  while (token->kind != TK_LINEBREAK)
  {
    if (token->kind == TK_RESERVED)
    {
      conditional_inclusion_type *type =
          malloc(sizeof(conditional_inclusion_type));
      *type = reserved_token_to_type(token, is_unary);
      if (*type == CPPTK_Parentheses_End)
      {
        conditional_inclusion_type *old = NULL;
        for (;;)
        {
          old = vector_pop(operator_stack);
          if (*old == CPPTK_Parentheses_Start)
            break;
          conditional_inclusion_token *new =
              calloc(1, sizeof(conditional_inclusion_token));
          new->type = *old;
          vector_push(output_list, new);
          is_unary = false;
        }
      }
      else
      {
        if (vector_has_data(operator_stack))
        {
          conditional_inclusion_type *old = vector_peek(operator_stack);
          if (*type != CPPTK_Parentheses_Start &&
              *old != CPPTK_Parentheses_Start)
          {
            size_t old_precedence = operator_precedence(*old);
            size_t type_precedence = operator_precedence(*type);
            // TODO ? :
            if (old_precedence > type_precedence ||
                (old_precedence == type_precedence &&
                 (old_precedence != 0 && old_precedence != 11)))
            {  // operator_stack から output_listへ移す
              conditional_inclusion_token *new =
                  calloc(1, sizeof(conditional_inclusion_token));
              if (old != vector_pop(operator_stack))
                unreachable();
              new->type = *old;
              vector_push(output_list, new);
            }
          }
        }
        is_unary = true;
        vector_push(operator_stack, type);
      }
    }
    else
    {
      conditional_inclusion_token *new =
          calloc(1, sizeof(conditional_inclusion_token));
      if (isdigit(token->str[0]))
      {
        new->type = CPPTK_Integer;
        long long *num = malloc(sizeof(long long));
        char *tmp;
        *num = strtoll(token->str, &tmp, 10);
        if (tmp != token->str + token->len)
          error_at(token->str, token->len, "cannot convert to integer");
        new->data = num;
      }
      else if (token->kind == TK_IDENT && token->len == 7 &&
               !strncmp(token->str, "defined", 7))
      {
        new->type = CPPTK_Integer;
        token = token_next_not_ignorable(token);
        bool is_brackets = false;
        if (token->kind == TK_RESERVED && token->str[0] == '(')
        {
          is_brackets = true;
          token = token_next_not_ignorable(token);
        }
        pr_debug2("defined operator found");
        long long *num = malloc(sizeof(long long));
        *num = find_macro_name_all(token) ? 1 : 0;
        new->data = num;
        if (is_brackets)
        {
          token = token_next_not_ignorable(token);
          if (token->kind != TK_RESERVED && token->str[0] == ')')
            error_at(token->str, token->len, "Invalid defined() use");
        }
      }
      else if (ident_replacement(token))
      {
        is_unary = false;
        continue;  // tokenを次に進めない
      }
      else if (token->kind == TK_IDENT)
      {
        new->type = CPPTK_Integer;
        long long *num = malloc(sizeof(long long));
        *num = 0;
        new->data = num;
      }
      else
        // TODO char
        error_at(token->str, token->len, "invalid token");
      vector_push(output_list, new);
      is_unary = false;
    }
    token = token_next_not_ignorable(token);
  }
  while (vector_has_data(operator_stack))
  {
    conditional_inclusion_token *new =
        calloc(1, sizeof(conditional_inclusion_token));
    new->type = *(conditional_inclusion_type *)(vector_pop(operator_stack));
    vector_push(output_list, new);
  }
  vector_free(operator_stack);
  clean_while_next(head, token);
}

static bool reverse_polish_notation_stack_machine()
{
  Vector *stack = vector_new();
  while (vector_has_data(output_list))
  {
    conditional_inclusion_token *token = vector_shift(output_list);
    switch (token->type)
    {
      case CPPTK_Integer:  // TODO char
        vector_push(stack, token);
        continue;
      case CPPTK_UnaryPlus:
      case CPPTK_UnaryMinus:
      case CPPTK_NOT:
      case CPPTK_Bitwise:
      {  // 単項右結合
        conditional_inclusion_token *tmp = vector_pop(stack);
        switch (token->type)
        {
          case CPPTK_UnaryPlus: break;
          case CPPTK_UnaryMinus:
            *(long long *)(tmp->data) = -*(long long *)(tmp->data);
            break;
          case CPPTK_NOT:
            *(long long *)(tmp->data) = !*(long long *)(tmp->data);
            break;
          case CPPTK_Bitwise:
            *(long long *)(tmp->data) = ~*(long long *)(tmp->data);
            break;
          default: unreachable(); break;
        }
        vector_push(stack, tmp);
      }
        continue;
      default:  // 2項左結合演算子
      {
        conditional_inclusion_token *lhs = vector_pop(stack);
        conditional_inclusion_token *rhs = vector_pop(stack);

        switch (token->type)
        {
          case CPPTK_Logical_OR:
            *((long long *)(rhs->data)) =
                *((long long *)(rhs->data)) || *((long long *)(lhs->data));
            break;
          case CPPTK_Logical_AND:
            *((long long *)(rhs->data)) =
                *((long long *)(rhs->data)) && *((long long *)(lhs->data));
            break;
          case CPPTK_Inclusive_OR:
            *((long long *)(rhs->data)) =
                *((long long *)(rhs->data)) | *((long long *)(lhs->data));
            break;
          case CPPTK_Exclusive_OR:
            *((long long *)(rhs->data)) =
                *((long long *)(rhs->data)) ^ *((long long *)(lhs->data));
            break;
          case CPPTK_AND:
            *((long long *)(rhs->data)) =
                *((long long *)(rhs->data)) & *((long long *)(lhs->data));
            break;
          case CPPTK_Equality:
            *((long long *)(rhs->data)) =
                *((long long *)(rhs->data)) == *((long long *)(lhs->data));
            break;
          case CPPTK_NEquality:
            *((long long *)(rhs->data)) =
                *((long long *)(rhs->data)) != *((long long *)(lhs->data));
            break;
          case CPPTK_LessThan:
            *((long long *)(rhs->data)) =
                *((long long *)(rhs->data)) < *((long long *)(lhs->data));
            break;
          case CPPTK_LessThanEq:
            *((long long *)(rhs->data)) =
                *((long long *)(rhs->data)) <= *((long long *)(lhs->data));
            break;
          case CPPTK_GreaterThan:
            *((long long *)(rhs->data)) =
                *((long long *)(rhs->data)) > *((long long *)(lhs->data));
            break;
          case CPPTK_GreaterThanEq:
            *((long long *)(rhs->data)) =
                *((long long *)(rhs->data)) >= *((long long *)(lhs->data));
            break;
          case CPPTK_LeftShift:
            *((long long *)(rhs->data)) = *((long long *)(rhs->data))
                                          << *((long long *)(lhs->data));
            break;
          case CPPTK_RightShift:
            *((long long *)(rhs->data)) =
                *((long long *)(rhs->data)) >> *((long long *)(lhs->data));
            break;
          case CPPTK_Plus:
            *((long long *)(rhs->data)) =
                *((long long *)(rhs->data)) + *((long long *)(lhs->data));
            break;
          case CPPTK_Minus:
            *((long long *)(rhs->data)) =
                *((long long *)(rhs->data)) - *((long long *)(lhs->data));
            break;
          case CPPTK_Mul:
            *((long long *)(rhs->data)) =
                *((long long *)(rhs->data)) * *((long long *)(lhs->data));
            break;
          case CPPTK_Div:
            *((long long *)(rhs->data)) =
                *((long long *)(rhs->data)) / *((long long *)(lhs->data));
            break;
          case CPPTK_DivReminder:
            *((long long *)(rhs->data)) =
                *((long long *)(rhs->data)) % *((long long *)(lhs->data));
            break;
          default: unreachable(); break;
        }
        vector_push(stack, rhs);
      }
        continue;
    }
  }
  bool result =
      *((long long *)((conditional_inclusion_token *)vector_pop(stack))->data);
  if (vector_has_data(stack))
    error_exit("invalid #if directive");
  return result;
}

/**
 *  C preprocessor #if expression interpreter
 *
 *  using Shunting-yard algorithm
 *
 *  EBNF:
 *  expression  = conditional
 *  conditional = logical_or ( "?" expression ":" expression )?
 *  logical_or = logical_and ( "||" logical_and )?
 *  logical_and = inclusive_or ( "&&" inclusive_or )?
 *  inclusive_or = exclusive_or ( "|" exclusive_or)?
 *  exclusive_or = and ( "^" and )?
 *  and = equality ( "&" equality )?
 *  equality = relational ( "==" relational | "!=" relational)?
 *  relational = shift ( "<" shift | ">" shift | "<=" shift | ">=" shift )?
 *  shift = additive ( "<<" additive | ">>" additive )?
 *  additive = multiplicative ( "+" multiplicative | "-" multiplicative )?
 *  multiplicative = unary ( "*" unary | "/" unary | "%" unary )?
 *  unary = ( "+" | "-" | "!" | "~" )? primary
 *  primary = integer |
 *            char |
 *            "(" expression ")" |
 *            identifier |
 *            "defined" identifier |
 *            "defined" "(" identifier ")"
 */
bool condition_interpreter(Token *head)
{
  pr_debug2("start #if token %.*s", head->len, head->str);
  shunting_yard_algorithm(head);
#ifdef DEBUG
  print_polish_notation();
#endif
  return reverse_polish_notation_stack_machine();
}

// 引数のheadトークンから nextトークンまでのトークンを消す
// ただし、TK_LINEBREAKは残す
static void clean_while_next(Token *head, Token *next)
{
  while (head != next)
  {
    if (head->kind == TK_EOF)
      error_exit("#ifディレクティブが閉じていません");
    if (head->kind != TK_LINEBREAK)
      token_void(head);
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
        Token *next = vector_shift(conditional_list);
        token_void(token);
        if (is_true)
          clean_while_next(token, next);
        next_conditional_inclusion(next, !is_true, conditional_list, true);
      }
      else
      {  // #elif
        if (is_end)
          error_at(token->str, token->len, "Invalid #elif use");
        if (!is_true)
          _conditional_inclusion(token_if, token, conditional_list);
        else
        {
          Token *next = vector_shift(conditional_list);
          clean_while_next(token, next);
          next_conditional_inclusion(next, is_true, conditional_list, false);
        }
      }
      break;
    case 6:  // #endif
      token_void(token);
      token_next_not_ignorable_void(token);
      return;
    case 8:  // #elifdef
    case 9:  // #elifndef
      if (is_end)
        error_at(token->str, token->len, "Invalid #elifdef use");
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
    default: unreachable(); break;
  }
}

static void _conditional_inclusion(if_directive type, Token *head,
                                   Vector *conditional_list)
{
  switch (type)
  {
    case token_if:
    {
      token_void(head);
      head = token_next_not_ignorable_void(head);
      bool is_true = condition_interpreter(head);
      Token *next = vector_shift(conditional_list);
      if (!is_true)
        clean_while_next(head, next);
      next_conditional_inclusion(next, is_true, conditional_list, false);
    }
    break;
    case token_ifdef:
    case token_ifndef:
    {
      token_void(head);
      head = token_next_not_ignorable_void(head);
      bool is_true = false;
      if (find_macro_name_all(head))
      {
        if (type == token_ifdef)
          is_true = true;
      }
      else if (type == token_ifndef)
        is_true = true;
      token_void(head);
      head = head->next;

      // #else #endif 等の対応するディレクティブ
      Token *next = vector_shift(conditional_list);
      if (!is_true)
        clean_while_next(head, next);
      next_conditional_inclusion(next, is_true, conditional_list, false);
    }
    break;
    default: unreachable();
  }
}

void conditional_inclusion(if_directive type, Vector *conditional_list)
{
  Token *head = vector_shift(conditional_list);
  _conditional_inclusion(type, head, conditional_list);
}