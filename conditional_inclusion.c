#include "include/conditional_inclusion.h"

#ifdef SELF_HOST
#include "test/compiler_header.h"
#else
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#endif

#include "include/debug.h"
#include "include/define.h"
#include "include/error.h"
#include "include/preprocessor.h"
#include "include/vector.h"

static void clean_while_next(Token *head, Token *next);

// Represents the precedence of right-associative operators
// Smaller values indicate higher precedence
static size_t operator_precedence(conditional_inclusion_type type)
{
  switch (type)
  {
    case CPPTK_Question:  // ?
    case CPPTK_Colon:     // :
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
          case '?': return CPPTK_Question;
          case ':': return CPPTK_Colon;
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
      if (token->kind == TK_CHAR)
      {  // Character processing
        conditional_inclusion_token *new =
            malloc(sizeof(conditional_inclusion_token));
        new->type = CPPTK_Integer;
        if (token->len == 2)
        {
          switch (token->str[1])
          {
            case 'n': new->num = '\n'; break;
            case 't': new->num = '\t'; break;
            case '\\': new->num = '\\'; break;
            case '\'': new->num = '\''; break;
            case '"': new->num = '\"'; break;
            case '0': new->num = '\0'; break;
            default: unreachable();
          }
        }
        else
          new->num = token->next->str[0];
        vector_push(output_list, new);
        is_unary = false;
        token_void(token);
        token = token_next_not_ignorable_void(token);
        continue;

        error_at(token->str, token->len, "Invalid #if directive.");
      }
      conditional_inclusion_type *type =
          malloc(sizeof(conditional_inclusion_type));
      *type = reserved_token_to_type(token, is_unary);
      if (*type == CPPTK_Parentheses_End || *type == CPPTK_Colon)
      {
        conditional_inclusion_type *old = NULL;
        for (;;)
        {
          old = vector_pop(operator_stack);
          if (*type == CPPTK_Parentheses_End ? *old == CPPTK_Parentheses_Start
                                             : *old == CPPTK_Question)
            break;
          conditional_inclusion_token *new =
              calloc(1, sizeof(conditional_inclusion_token));
          new->type = *old;
          vector_push(output_list, new);
        }
        is_unary = *type == CPPTK_Colon;
        if (*type == CPPTK_Colon)
          vector_push(operator_stack, type);
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
            if (old_precedence > type_precedence ||
                (old_precedence == type_precedence &&
                 (old_precedence != 0 && old_precedence != 11)))
            {  // Move from operator_stack to output_list
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
      if (token->len && isdigit(token->str[0]))
      {
        bool is_hex = false;
        if (!strncmp(token->str, "0x", 2))
          is_hex = true;
        new->type = CPPTK_Integer;
        char *tmp;
        new->num = strtoll(token->str, &tmp, 0);
        while (*tmp == 'u' || *tmp == 'U' || *tmp == 'l' || *tmp == 'L')
          tmp++;
        if (tmp != token->str + token->len + (is_hex ? token->next->len : 0))
          error_at(token->str, token->len, "cannot convert to integer");
        if (is_hex)
          token = token->next;
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
        pr_debug2("Defined operator found");
        new->num = find_macro_name_all(token) ? 1 : 0;
        if (is_brackets)
        {
          token = token_next_not_ignorable(token);
          if (token->kind != TK_RESERVED && token->str[0] == ')')
            error_at(token->str, token->len, "Invalid defined() use");
        }
      }
      else if (ident_replacement(token))
      {
        while (token->kind == TK_IGNORABLE || token->kind == TK_ILB)
        {
          token = token->next;
        }
        continue;
      }
      else if (token->kind == TK_IDENT)
      {
        new->type = CPPTK_Integer;
        new->num = 0;
      }
      else
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
      {  // Unary right-associative
        conditional_inclusion_token *tmp = vector_pop(stack);
        if (tmp->type != CPPTK_Integer)
          error_exit("Invalid #if directive.");
        switch (token->type)
        {
          case CPPTK_UnaryPlus: break;
          case CPPTK_UnaryMinus: tmp->num = -tmp->num; break;
          case CPPTK_NOT: tmp->num = !tmp->num; break;
          case CPPTK_Bitwise: tmp->num = ~tmp->num; break;
          default: unreachable(); break;
        }
        vector_push(stack, tmp);
      }
        continue;
      case CPPTK_Question: unreachable(); break;
      case CPPTK_Colon:  // Binary operator
      {
        conditional_inclusion_token *left_hand_side = vector_pop(stack);
        conditional_inclusion_token *middle_hand_side = vector_pop(stack);
        conditional_inclusion_token *right_hand_side = vector_pop(stack);
        if (left_hand_side->type != CPPTK_Integer ||
            middle_hand_side->type != CPPTK_Integer ||
            right_hand_side->type != CPPTK_Integer)
          error_exit("Invalid #if directive.");
        left_hand_side->num =
            left_hand_side->num ? middle_hand_side->num : right_hand_side->num;
        vector_push(stack, left_hand_side);
      }
        continue;
      default:  // Binary left-associative operator
      {
        conditional_inclusion_token *lhs = vector_pop(stack);
        conditional_inclusion_token *rhs = vector_pop(stack);
        if (lhs->type != CPPTK_Integer || rhs->type != CPPTK_Integer)
          error_exit("Invalid #if directive.");
        switch (token->type)
        {
          case CPPTK_Logical_OR: rhs->num = rhs->num || lhs->num; break;
          case CPPTK_Logical_AND: rhs->num = rhs->num && lhs->num; break;
          case CPPTK_Inclusive_OR: rhs->num = rhs->num | lhs->num; break;
          case CPPTK_Exclusive_OR: rhs->num = rhs->num ^ lhs->num; break;
          case CPPTK_AND: rhs->num = rhs->num & lhs->num; break;
          case CPPTK_Equality: rhs->num = rhs->num == lhs->num; break;
          case CPPTK_NEquality: rhs->num = rhs->num != lhs->num; break;
          case CPPTK_LessThan: rhs->num = rhs->num < lhs->num; break;
          case CPPTK_LessThanEq: rhs->num = rhs->num <= lhs->num; break;
          case CPPTK_GreaterThan: rhs->num = rhs->num > lhs->num; break;
          case CPPTK_GreaterThanEq: rhs->num = rhs->num >= lhs->num; break;
          case CPPTK_LeftShift: rhs->num = rhs->num << lhs->num; break;
          case CPPTK_RightShift: rhs->num = rhs->num >> lhs->num; break;
          case CPPTK_Plus: rhs->num = rhs->num + lhs->num; break;
          case CPPTK_Minus: rhs->num = rhs->num - lhs->num; break;
          case CPPTK_Mul: rhs->num = rhs->num * lhs->num; break;
          case CPPTK_Div: rhs->num = rhs->num / lhs->num; break;
          case CPPTK_DivReminder: rhs->num = rhs->num % lhs->num; break;
          default: unreachable(); break;
        }
        vector_push(stack, rhs);
      }
        continue;
    }
  }
  bool result = ((conditional_inclusion_token *)vector_pop(stack))->num;
  if (vector_has_data(stack))
    error_exit("Invalid #if directive.");
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
#ifdef DEBUG
  pr_debug("Condition interpreter");
  Token *ptr = head;
  while (ptr->kind != TK_LINEBREAK)
  {
    printf("%.*s", (int)ptr->len, ptr->str);
    ptr = ptr->next;
  }
  printf("\n");
#endif
  shunting_yard_algorithm(head);
#ifdef DEBUG
  print_polish_notation();
#endif
  return reverse_polish_notation_stack_machine();
}

// Deletes tokens from the head token to the next token in the argument.
// However, TK_LINEBREAK is retained.
static void clean_while_next(Token *head, Token *next)
{
  while (head != next)
  {
    if (head->kind == TK_EOF)
      error_exit("#if directive is not closed.");
    if (head->kind != TK_LINEBREAK)
      token_void(head);
    head = head->next;
  }
}

static void _conditional_inclusion(if_directive type, Token *head,
                                   Vector *conditional_list);

// Function to process #else #elif #endif etc. corresponding to #if #ifdef
// #ifndef
void next_conditional_inclusion(Token *token, bool is_true,
                                Vector *conditional_list, bool is_end)
{
  switch (token->len)
  {
    case 4:  // #else #elif
      if (!strncmp(token->str, "else", 4))
      {  // #else
        Token *next = vector_shift(conditional_list);
        token_void(next);
        next = token_next_not_ignorable_void(next);
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
          token_void(next);
          next = token_next_not_ignorable_void(next);
          clean_while_next(token, next);
          next_conditional_inclusion(next, is_true, conditional_list, false);
        }
      }
      break;
    case 5:  // #endif
      token_void(token);
      token_next_not_ignorable_void(token);
      return;
    case 7:  // #elifdef
    case 8:  // #elifndef
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
        token_void(next);
        next = token_next_not_ignorable_void(next);
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
      token_void(next);
      next = token_next_not_ignorable_void(next);
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

      // Corresponding directives like #else #endif
      Token *next = vector_shift(conditional_list);
      token_void(next);
      next = token_next_not_ignorable_void(next);
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
  token_void(head);
  head = token_next_not_ignorable_void(head);
  _conditional_inclusion(type, head, conditional_list);
}