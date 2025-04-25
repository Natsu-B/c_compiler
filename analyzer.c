// ------------------------------------------------------------------------------------
// semantic analyzer
// ------------------------------------------------------------------------------------

#include "include/analyzer.h"

#include <stdlib.h>

#include "include/debug.h"
#include "include/error.h"
#include "include/generator.h"
#include "include/offset.h"
#include "include/parser.h"
#include "include/type.h"
#include "include/variables.h"

bool is_equal_type(Type *lhs, Type *rhs)
{
  if (lhs->type == rhs->type)
  {
    if (lhs->type == TYPE_PTR || lhs->type == TYPE_ARRAY)
    {
      if (lhs->ptr_to && rhs->ptr_to)
        return is_equal_type(lhs->ptr_to, rhs->ptr_to);
      else
        false;
    }
    return true;
  }
  return false;
}

TypeKind implicit_type_conversion_assign(Type *lhs, Type *rhs)
{
  if (rhs->type == TYPE_PTR || rhs->type == TYPE_ARRAY)
  {
    if (lhs->type == TYPE_PTR || lhs->type == TYPE_ARRAY)
      return TYPE_PTR;
  }
  else
  {
    switch (lhs->type)
    {
      case TYPE_STR:
      case TYPE_STRUCT:
      case TYPE_PTR:
      case TYPE_ARRAY:
      case TYPE_NULL: break;
      default: return lhs->type;
    }
  }
  return TYPE_NULL;
}

void add_type(Node *node)
{
  if (node->lhs)
    add_type(node->lhs);
  if (node->rhs)
    add_type(node->rhs);
  if (node->condition)
    add_type(node->condition);
  if (node->true_code)
    add_type(node->true_code);
  if (node->false_code)  // node->init も同じ
    add_type(node->false_code);
  if (node->update)
    add_type(node->update);
  if (node->expr)
    for (NDBlock *tmp = node->expr; tmp; tmp = tmp->next)
      add_type(tmp->node);
  if (node->stmt)
    for (NDBlock *tmp = node->stmt; tmp; tmp = tmp->next)
      add_type(tmp->node);

  if (node->kind == ND_VAR)
  {
    node->type = node->var->type;
    return;
  }
  if (node->kind == ND_MUL || node->kind == ND_DIV || node->kind == ND_FUNCCALL)
  {
    // TODO
    node->type = alloc_type(TYPE_INT);
    return;
  }
  if (node->kind == ND_ADDR)
  {
    Type *ptr = alloc_type(TYPE_PTR);
    ptr->ptr_to = node->lhs->type;
    node->lhs->type = ptr;
    node->type = ptr;
    return;
  }
  if (node->kind == ND_DEREF)
  {
    if (node->lhs->type->type != TYPE_PTR &&
        node->lhs->type->type != TYPE_ARRAY)
      error_at(node->token->str, node->token->len, "invalid dereference");
    node->type = node->lhs->type->ptr_to;
    return;
  }
  if (node->kind == ND_ADD)
  {
    int flag = 0;
    Type *type = alloc_type(TYPE_INT);
    if (node->lhs->type->type != TYPE_INT)
    {
      type = node->lhs->type;
      flag++;
    }
    if (node->rhs->type->type != TYPE_INT)
    {
      type = node->rhs->type;
      flag++;
    }
    if (flag >= 2)
      if (node->lhs->type->type == TYPE_PTR ||
          node->lhs->type->type == TYPE_ARRAY ||
          node->rhs->type->type == TYPE_PTR ||
          node->rhs->type->type == TYPE_ARRAY)
        error_at(node->token->str, node->token->len,
                 "invalid use of the '+' operator");
    node->lhs->type = type;
    node->rhs->type = type;
    node->type = type;
    return;
  }
  if (node->kind == ND_SUB)
  {
    if (node->rhs->type->type == TYPE_PTR)
      error_at(node->token->str, node->token->len,
               "invalid use of the '-' operator");
    if (node->lhs->type->type == TYPE_PTR)
    {
      node->rhs->type = node->lhs->type;
      node->type = node->lhs->type;
    }
    else
      node->type = alloc_type(TYPE_INT);
    return;
  }
  if (node->kind == ND_SIZEOF)
  {
    node->type = alloc_type(TYPE_LONG);
    return;
  }
  if (node->kind == ND_ARRAY)
  {
    if (node->lhs->is_new)
    {
      // old1 -> old2 -> old3 ... のように並んでいた Typeを
      // old1 -> new -> old2 -> old3 ... に変更することで同じ変数の
      // Typeを変更する
      Type *new = alloc_type(node->lhs->type->type);
      new->ptr_to = node->lhs->type->ptr_to;
      new->size = node->rhs->val;
      node->lhs->type->type = TYPE_ARRAY;
      node->lhs->type->ptr_to = new;
      node->lhs->type->size = node->rhs->val;
      node->rhs->type = node->lhs->type;
      node->rhs->type = node->lhs->type;
      node->lhs->val = node->rhs->val;
    }
    else
    {
      // char *i = "hoge"; i[3]; 等が使えなくなるためコメントアウト
      // if (node->rhs->val < 0 || node->rhs->val >= node->lhs->type->size)
      //     error_at(node->token->str, "index out of bounds for type array");
      node->kind = ND_DEREF;
      node->lhs = new_node(ND_ADD, node->lhs, node->rhs, node->token);
      node->rhs = NULL;
      node->lhs->type = node->lhs->lhs->type;
      add_type(node);
    }
    return;
  }
}

void analyze_type(Node *node)
{
  if (node->lhs)
    analyze_type(node->lhs);
  if (node->rhs)
    analyze_type(node->rhs);
  if (node->init)
    analyze_type(node->init);
  if (node->condition)
    analyze_type(node->condition);
  if (node->true_code)
    analyze_type(node->true_code);
  if (node->false_code)  // node->init も同じ
    analyze_type(node->false_code);
  if (node->update)
    analyze_type(node->update);
  if (node->expr)
    for (NDBlock *tmp = node->expr; tmp; tmp = tmp->next)
      analyze_type(tmp->node);
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
    case ND_ASSIGN:
      if (!is_equal_type(node->lhs->type, node->rhs->type) &&
          node->rhs->kind != ND_STRING)
      {
        TypeKind converted_type =
            implicit_type_conversion_assign(node->lhs->type, node->rhs->type);
        if (converted_type == TYPE_NULL)
          error_at(node->token->str, node->token->len,
                   "cannot convert both sides of '=' types");
        node->type = alloc_type(converted_type);
      }
      else
        node->type = node->lhs->type;
      break;

    case ND_NUM:
      if (node->type->type == TYPE_PTR)
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
            case TYPE_INT:
            case TYPE_LONG:
            case TYPE_CHAR:
            case TYPE_PTR:
              node->var->offset = calculate_offset(size_of(node->type));
              break;
            case TYPE_ARRAY:
              node->var->offset = calculate_offset(size_of(node->type->ptr_to) *
                                                   node->type->size);
              break;
            default: error_exit("unreachable"); break;
          }
        }
      }
      break;

    case ND_SIZEOF:
      node->kind = ND_NUM;
      switch (node->lhs->type->type)
      {
        case TYPE_PTR:
        case TYPE_INT:
        case TYPE_LONG: node->val = size_of(node->lhs->type); break;
        case TYPE_CHAR: node->val = size_of(node->lhs->type); break;
        case TYPE_ARRAY:
          node->val = size_of(node->lhs->type) * node->lhs->val;
          break;
        default: error_exit("unreachable"); break;
      }
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
    if (node->kind == ND_FUNCDEF)
    {
      for (NDBlock *tmp = node->expr; tmp; tmp = tmp->next)
      {
        add_type(tmp->node);
      }
      for (NDBlock *tmp = node->stmt; tmp; tmp = tmp->next)
      {
        add_type(tmp->node);
      }
    }
    else if (node->kind == ND_VAR)
    {
      if (node->var->is_local)
        error_exit("failed to parse correctly");
      add_type(node);
    }
    else
      error_exit("unreachable");
  }
#ifdef DEBUG
  print_parse_result(funcblock);
#endif
  // add_typeでつけた型に応じてオフセットの計算等をする
  for (FuncBlock *pointer = funcblock; pointer; pointer = pointer->next)
  {
    Node *node = pointer->node;
    if (node->kind == ND_FUNCDEF)
    {
      init_nest();
      for (NDBlock *tmp = node->expr; tmp; tmp = tmp->next)
      {
        analyze_type(tmp->node);
      }
      for (NDBlock *tmp = node->stmt; tmp; tmp = tmp->next)
      {
        analyze_type(tmp->node);
      }
      // stacksizeは8byte単位で揃える
      size_t max_stacksize = get_max_offset();
      pointer->stacksize =
          max_stacksize % 8 ? (max_stacksize / 8 + 1) * 8 : max_stacksize;
    }
    else if (node->kind == ND_VAR)
      analyze_type(node);
    else
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