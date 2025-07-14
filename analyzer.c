// ------------------------------------------------------------------------------------
// semantic analyzer
// ------------------------------------------------------------------------------------

#include "include/analyzer.h"

#ifdef SELF_HOST
#include "test/compiler_header.h"
#else
#include <stdlib.h>
#endif

#include "include/debug.h"
#include "include/error.h"
#include "include/generator.h"
#include "include/offset.h"
#include "include/parser.h"
#include "include/type.h"
#include "include/variables.h"

bool is_pointer_type(Type *t)
{
  return t->type == TYPE_PTR || t->type == TYPE_ARRAY;
}

bool is_integer_type(Type *t)
{
  return t->type == TYPE_BOOL || t->type == TYPE_CHAR ||
         t->type == TYPE_SHORT || t->type == TYPE_INT || t->type == TYPE_LONG ||
         t->type == TYPE_LONGLONG || t->type == TYPE_ENUM;
}

bool is_equal_type(Type *lhs, Type *rhs)
{
  if (is_pointer_type(lhs) && is_pointer_type(rhs))
    return is_equal_type(lhs->ptr_to, rhs->ptr_to);

  if (lhs->type != rhs->type)
    return false;

  if (is_integer_type(lhs))
    return lhs->is_signed == rhs->is_signed;

  return true;
}

Type *implicit_type_conversion(Type *lhs, Type *rhs)
{
  // long long > long >  int
  // bool, short, and char are promoted to int through integer promotion

  // long long
  if (lhs->type == TYPE_LONGLONG || rhs->type == TYPE_LONGLONG)
  {
    Type *t = alloc_type(TYPE_LONGLONG);
    t->is_signed = lhs->is_signed && rhs->is_signed;
    return t;
  }

  // long
  if (lhs->type == TYPE_LONG || rhs->type == TYPE_LONG)
  {
    Type *t = alloc_type(TYPE_LONG);
    t->is_signed = lhs->is_signed && rhs->is_signed;
    return t;
  }

  // int
  if (lhs->type == TYPE_INT || rhs->type == TYPE_INT)
  {
    Type *t = alloc_type(TYPE_INT);
    t->is_signed = lhs->is_signed && rhs->is_signed;
    return t;
  }

  if (!is_integer_type(lhs) || !is_integer_type(rhs))
    unreachable();

  // integer promotion
  Type *t = alloc_type(TYPE_INT);
  t->is_signed = true;
  return t;
}

struct
{
  Vector *switch_info_list;
  Vector *case_list;
} switch_list;

Vector *switch_new(GTLabel *name)
{
  if (!switch_list.case_list)
  {
    switch_list.case_list = vector_new();
    switch_list.switch_info_list = vector_new();
  }
  vector_push(switch_list.switch_info_list, name);
  Vector *tmp = vector_new();
  vector_push(switch_list.case_list, tmp);
  return tmp;
}

GTLabel *switch_add(Node *node, size_t *num)
{
  if (!switch_list.case_list || !vector_size(switch_list.case_list))
    error_at(node->token->str, node->token->len, "invalid case statement");
  *num = vector_size(vector_peek(switch_list.case_list));
  vector_push(vector_peek(switch_list.case_list), node);
  return vector_peek(switch_list.switch_info_list);
}

void switch_end()
{
  vector_pop(switch_list.switch_info_list);
  vector_pop(switch_list.case_list);
}

void add_type(Node *node);

void add_type_internal(Node *node)
{
  switch (node->kind)
  {
    case ND_NOP: return;
    case ND_ADD:
    {
      if (is_pointer_type(node->lhs->type) && is_pointer_type(node->rhs->type))
        error_at(node->token->str, node->token->len,
                 "invalid operands to binary +");

      if (is_pointer_type(node->lhs->type))
      {  // ptr + int
        node->rhs = new_node(ND_MUL, node->rhs,
                             new_node_num(size_of(node->lhs->type->ptr_to)),
                             node->token);
        add_type(node->rhs);
        node->type = node->lhs->type;
        return;
      }
      if (is_pointer_type(node->rhs->type))
      {  // int + ptr
        node->lhs = new_node(ND_MUL, node->lhs,
                             new_node_num(size_of(node->rhs->type->ptr_to)),
                             node->token);
        add_type(node->lhs);
        node->type = node->rhs->type;
        return;
      }

      node->type = implicit_type_conversion(node->lhs->type, node->rhs->type);
      return;
    }

    case ND_SUB:
      if (is_pointer_type(node->lhs->type) && is_pointer_type(node->rhs->type))
      {
        // ptr - ptr
        Node *n = new_node(ND_SUB, node->lhs, node->rhs, node->token);
        n->lhs->type = n->rhs->type = alloc_type(TYPE_LONG);
        node->lhs =
            new_node(ND_DIV, n, new_node_num(size_of(node->lhs->type->ptr_to)),
                     node->token);
        add_type(node->lhs);
        node->kind = node->lhs->kind;
        node->rhs = node->lhs->rhs;
        node->type = alloc_type(TYPE_LONG);
        node->type->is_signed = true;
        return;
      }
      if (is_pointer_type(node->lhs->type))
      {  // ptr - int
        node->rhs = new_node(ND_MUL, node->rhs,
                             new_node_num(size_of(node->lhs->type->ptr_to)),
                             node->token);
        add_type(node->rhs);
        node->type = node->lhs->type;
        return;
      }
      if (is_pointer_type(node->rhs->type))
      {
        error_at(node->token->str, node->token->len,
                 "invalid operands to binary -");
      }

      node->type = implicit_type_conversion(node->lhs->type, node->rhs->type);
      return;
    case ND_MUL:
    case ND_DIV:
    case ND_IDIV:
    {
      if (!is_integer_type(node->lhs->type) ||
          !is_integer_type(node->rhs->type))
        error_at(node->token->str, node->token->len,
                 "operands for '%s' must be integer types",
                 (node->kind == ND_MUL ? "*" : "/"));
      node->type = implicit_type_conversion(node->lhs->type, node->rhs->type);
      return;
    }
    case ND_EQ:
    case ND_NEQ:
    case ND_LT:
    case ND_LTE: node->type = alloc_type(TYPE_BOOL); return;
    case ND_ASSIGN:
    {
      Type *lhs_type = node->lhs->type;
      Type *rhs_type = node->rhs->type;

      if (is_equal_type(lhs_type, rhs_type))
      {
        node->type = lhs_type;
        return;
      }

      // when lhs type is bool, we should eval rhs value before assigned
      if (lhs_type->type == TYPE_BOOL)
      {
        Node *new = new_node(ND_EVAL, node->rhs, NULL, NULL);
        node->rhs = new;
        rhs_type = lhs_type;
        node->type = lhs_type;
        return;
      }

      if (is_pointer_type(lhs_type) && is_pointer_type(rhs_type))
      {
        if ((lhs_type->ptr_to && lhs_type->ptr_to->type == TYPE_VOID) ||
            (rhs_type->ptr_to && rhs_type->ptr_to->type == TYPE_VOID))
        {
          node->type = lhs_type;
          return;
        }
      }

      // T* = 0
      if (is_pointer_type(lhs_type) && rhs_type->type == TYPE_INT &&
          node->rhs->kind == ND_NUM && node->rhs->val == 0)
      {
        node->type = lhs_type;
        return;
      }

      if (is_integer_type(lhs_type) && is_integer_type(rhs_type))
      {
        node->type = lhs_type;
        return;
      }

      if (node->rhs->kind == ND_STRING)
      {
        node->type = lhs_type;
        return;
      }

      error_at(node->token->str, node->token->len,
               "cannot convert both sides of '=' types");
      return;
    }
    case ND_ADDR:
    {
      Type *ptr = alloc_type(TYPE_PTR);
      ptr->ptr_to = node->lhs->type;
      node->type = ptr;
      return;
    }
    case ND_DEREF:
      if (!is_pointer_type(node->lhs->type))
        error_at(node->token->str, node->token->len, "invalid dereference");
      if (!node->lhs->type->ptr_to)
        error_at(node->token->str, node->token->len,
                 "cannot dereference a void pointer");
      node->type = node->lhs->type->ptr_to;
      size_of(node->type);  // check if it's a valid type
      return;
    case ND_PREINCREMENT:
    case ND_PREDECREMENT:
    case ND_POSTINCREMENT:
    case ND_POSTDECREMENT:
      if (!is_integer_type(node->lhs->type) &&
          !is_pointer_type(node->lhs->type))
        error_at(node->token->str, node->token->len,
                 "operand must be integer or pointer for inc/dec");
      node->type = node->lhs->type;
      if (node->lhs->type->ptr_to)
        node->val = size_of(node->lhs->type->ptr_to);
      return;
    case ND_FUNCDEF: node->type = alloc_type(TYPE_VOID); return;
    case ND_FUNCCALL:
    {
      Var *fn = find_var(node->token);
      if (fn && fn->type->param_list)
        node->type = vector_peek_at(fn->type->param_list, 1);
      else
        node->type = alloc_type(TYPE_INT);
      return;
    }
    case ND_RETURN: node->type = alloc_type(TYPE_VOID); return;
    case ND_SIZEOF:
    {
      Type *t = alloc_type(TYPE_LONG);
      t->is_signed = false;
      node->type = t;
      return;
    }
    case ND_IF:
    case ND_ELIF:
    case ND_FOR:
    case ND_WHILE:
    case ND_DO: node->type = alloc_type(TYPE_VOID); return;
    case ND_TERNARY:
    {
      Type *lhs_type = node->chs->type;
      Type *rhs_type = node->rhs->type;

      if (is_equal_type(lhs_type, rhs_type))
      {
        node->type = lhs_type;
        return;
      }

      if (is_pointer_type(lhs_type) && is_pointer_type(rhs_type))
      {
        // T* and void* -> void*
        if (!lhs_type->ptr_to || !rhs_type->ptr_to)
        {
          Type *t = alloc_type(TYPE_PTR);
          t->ptr_to = NULL;
          node->type = t;
          return;
        }
      }

      if (is_integer_type(lhs_type) && is_integer_type(rhs_type))
      {
        node->type = implicit_type_conversion(lhs_type, rhs_type);
        return;
      }

      error_at(node->token->str, node->token->len,
               "mismatched types in ternary operator");
      return;
    }
    case ND_LOGICAL_OR:
    case ND_LOGICAL_AND:
    {
      Type *lhs = node->lhs->type;
      Type *rhs = node->rhs->type;
      if (!is_integer_type(lhs) && !is_pointer_type(lhs))
        error_at(node->lhs->token->str, node->lhs->token->len,
                 "scalar type required");
      if (!is_integer_type(rhs) && !is_pointer_type(rhs))
        error_at(node->rhs->token->str, node->rhs->token->len,
                 "scalar type required");
      node->type = alloc_type(TYPE_BOOL);
      return;
    }
    case ND_INCLUSIVE_OR:
    case ND_EXCLUSIVE_OR:
    case ND_AND:
      if (!is_integer_type(node->lhs->type) ||
          !is_integer_type(node->rhs->type))
        error_at(node->token->str, node->token->len,
                 "operands for bitwise op must be integer types");
      node->type = implicit_type_conversion(node->lhs->type, node->rhs->type);
      return;
    case ND_LEFT_SHIFT:
    case ND_RIGHT_SHIFT:
      if (!is_integer_type(node->lhs->type) ||
          !is_integer_type(node->rhs->type))
        error_at(node->token->str, node->token->len,
                 "operands for shift op must be integer types");
      node->type = node->lhs->type;
      return;
    case ND_ASSIGNMENT:
      node->kind = ND_ASSIGN;
      if (!node->rhs->lhs || node->rhs->lhs->kind != ND_VAR)
        error_at(node->token->str, node->token->len, "invalid token");
      node->lhs = node->rhs->lhs;
      add_type_internal(node);
      return;
    case ND_VAR: node->type = node->var->type; return;
    case ND_ARRAY:
      node->kind = ND_DEREF;
      node->lhs = new_node(ND_ADD, node->lhs, node->rhs, node->token);
      node->rhs = NULL;
      add_type(node);
      add_type_internal(node);
      return;
    case ND_DOT:
    case ND_ARROW:
    {
      size_t offset = 0;
      node->type = find_struct_child(node->lhs, node->rhs, &offset);
      node->child_offset = offset;
      return;
    }
    case ND_FIELD: node->type = alloc_type(TYPE_VOID); return;
    case ND_TYPE_NAME: return;
    case ND_NUM: node->type = alloc_type(TYPE_INT); return;
    case ND_BLOCK: node->type = alloc_type(TYPE_VOID); return;
    case ND_DISCARD_EXPR: node->type = alloc_type(TYPE_VOID); return;
    case ND_STRING: node->type = alloc_type(TYPE_STR); return;
    case ND_GOTO:
    case ND_LABEL:
    case ND_SWITCH: node->type = alloc_type(TYPE_VOID); return;
    case ND_CASE:
    {
      size_t num;
      node->switch_name = switch_add(node, &num);
      node->case_num = num;
      node->type = alloc_type(TYPE_VOID);
      return;
    }
    case ND_COMMA: node->type = node->rhs->type; return;
    case ND_CAST: node->type = node->rhs->type; return;
    default: unreachable();
  }
}

void add_type(Node *node)
{
  if (!node)
    return;
  if (node->kind == ND_SWITCH)
    node->case_list = switch_new(node->name);
  if (node->lhs)
    add_type(node->lhs);
  if (node->chs)
    add_type(node->chs);
  if (node->rhs)
    add_type(node->rhs);
  if (node->condition)
    add_type(node->condition);
  if (node->true_code)
    add_type(node->true_code);
  if (node->false_code)
    add_type(node->false_code);
  if (node->init)
    add_type(node->init);
  if (node->update)
    add_type(node->update);
  if (node->statement_child)
    add_type(node->statement_child);
  if (node->expr)
    for (size_t i = 1; i <= vector_size(node->expr); i++)
      add_type(vector_peek_at(node->expr, i));
  if (node->stmt)
    for (NDBlock *tmp = node->stmt; tmp; tmp = tmp->next)
      add_type(tmp->node);
  if (node->kind == ND_SWITCH)
    switch_end();

  add_type_internal(node);
}

void analyze_type(Node *node)
{
  if (!node)
    return;
  if (node->lhs)
    analyze_type(node->lhs);
  if (node->rhs)
    analyze_type(node->rhs);
  if (node->chs)
    analyze_type(node->chs);
  if (node->init)
    analyze_type(node->init);
  if (node->condition)
    analyze_type(node->condition);
  if (node->true_code)
    analyze_type(node->true_code);
  if (node->false_code)
    analyze_type(node->false_code);
  if (node->init)
    analyze_type(node->init);
  if (node->update)
    analyze_type(node->update);
  if (node->statement_child)
    analyze_type(node->statement_child);
  if (node->expr)
    for (size_t i = 1; i <= vector_size(node->expr); i++)
      analyze_type(vector_peek_at(node->expr, i));
  if (node->stmt)
  {  // ND_BLOCKのとき
    offset_enter_nest();
    for (NDBlock *tmp = node->stmt; tmp; tmp = tmp->next)
      analyze_type(tmp->node);
    offset_exit_nest();
  }

  switch (node->kind)
  {
    case ND_STRING:
      // TODO ND_ARRAYのときは挙動が異なる
      node->literal_name = add_string_literal(node->token);
      break;

    case ND_NUM:
      if (node->type->type == TYPE_PTR || node->type->type == TYPE_ARRAY)
      {
        node->val = node->val * size_of(node->type->ptr_to);
      }
      break;

    case ND_VAR:
      if (node->var->is_local)
      {
        if (node->is_new)
        {
          switch (node->type->type)
          {
            case TYPE_STRUCT:
              node->var->offset = calculate_offset(size_of(node->type));
              break;
            case TYPE_ARRAY:
              node->var->offset = calculate_offset(size_of(node->type->ptr_to) *
                                                   node->type->size);
              break;
            default:
              node->var->offset = calculate_offset(size_of(node->type));
              break;
          }
        }
      }
      break;

    case ND_SIZEOF:
      node->kind = ND_NUM;
      if (node->lhs->kind == ND_TYPE_NAME)
        node->val = size_of(node->lhs->type);
      else if (node->lhs->type->type == TYPE_ARRAY)
        node->val = size_of(node->lhs->type) * node->lhs->val;
      else
        node->val = size_of(node->lhs->type);
      break;

    default: break;
  }
}

FuncBlock *analyzer(FuncBlock *funcblock)
{
  pr_debug("start analyze");
  // ND_ASSIGN等の変数や数字でないnodeに型をつける
  for (FuncBlock *pointer = funcblock; pointer; pointer = pointer->next)
  {
    Node *node = pointer->node;
    if (!node)
      continue;
    if (node->kind == ND_FUNCDEF)
    {
      for (size_t i = 1; i <= vector_size(node->expr); i++)
        add_type(vector_peek_at(node->expr, i));
      for (NDBlock *tmp = node->stmt; tmp; tmp = tmp->next)
        add_type(tmp->node);
    }
    else if (node->kind == ND_VAR)
    {
      if (node->var->is_local)
        error_exit("failed to parse correctly");
      add_type(node);
    }
    else if (node->kind != ND_NOP)
      error_exit("unreachable");
  }
#ifdef DEBUG
  print_parse_result(funcblock);
#endif
  // add_typeでつけた型に応じてオフセットの計算等をする
  for (FuncBlock *pointer = funcblock; pointer; pointer = pointer->next)
  {
    Node *node = pointer->node;
    if (!node)
      continue;
    if (node->kind == ND_FUNCDEF)
    {
      init_nest();
      for (size_t i = 1; i <= vector_size(node->expr); i++)
        analyze_type(vector_peek_at(node->expr, i));
      for (NDBlock *tmp = node->stmt; tmp; tmp = tmp->next)
        analyze_type(tmp->node);
      // stacksizeは8byte単位で揃える
      size_t max_stacksize = get_max_offset();
      pointer->stacksize =
          max_stacksize % 8 ? (max_stacksize / 8 + 1) * 8 : max_stacksize;
    }
    else if (node->kind == ND_VAR)
      analyze_type(node);
    else if (node->kind != ND_NOP)
      error_exit("unreachable");
  }
#ifdef DEBUG
  print_parse_result(funcblock);
#endif
  // グローバル変数の初期化方法を決める
  // TODO ゼロクリアと文字列以外対応していない
  Var *pointer = get_global_var();
  for (; pointer; pointer = pointer->next)
  {
    if (pointer->how2_init == reserved)
      pointer->how2_init = init_zero;
  }
  return funcblock;
}