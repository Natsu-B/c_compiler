#include "include/eval_constant.h"

#include "include/debug.h"
#include "include/error.h"
#include "include/parser.h"
#include "include/tokenizer.h"
#include "include/type.h"

long _eval_constant_expression(Node* node)
{
  switch (node->kind)
  {
    case ND_ADD:
      return _eval_constant_expression(node->lhs) +
             _eval_constant_expression(node->rhs);
    case ND_SUB:
      return _eval_constant_expression(node->lhs) -
             _eval_constant_expression(node->rhs);
    case ND_MUL:
      return _eval_constant_expression(node->lhs) *
             _eval_constant_expression(node->rhs);
    case ND_DIV:
      return _eval_constant_expression(node->lhs) /
             _eval_constant_expression(node->rhs);
    case ND_NUM: return node->val;
    default:
    {
      Token* token = peek_ident();
      size_t enum_num = 0;
      if (is_enum_or_function_or_typedef_name(token, &enum_num) == enum_member_name)
      {
        consume_ident();
        return enum_num;
      }
      unimplemented();
    }
    break;
  }
  unreachable();
  return 0;
}

long eval_constant_expression()
{
  Node* node = constant_expression();
  pr_debug2("start constant expression evaluator");
#if DEBUG
  FuncBlock tmp;
  tmp.next = NULL;
  tmp.node = node;
  tmp.stacksize = 0;
  print_parse_result(&tmp);
#endif
  return _eval_constant_expression(node);
}