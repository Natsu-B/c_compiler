#include "include/optimizer.h"

#include <limits.h>

#include "include/parser.h"

static Node* constant_folding(Node* node)
{
  if (!node)
    return NULL;
  if (node->lhs)
    node->lhs = constant_folding(node->lhs);
  if (node->rhs)
    node->rhs = constant_folding(node->rhs);
  if (node->kind == ND_BLOCK)
    for (NDBlock* tmp = node->func.stmt; tmp; tmp = tmp->next)
      tmp->node = constant_folding(tmp->node);
  visit_children(node, constant_folding);

  switch (node->kind)
  {
    case ND_ADD:
    case ND_SUB:
    case ND_MUL:
    case ND_DIV:
    case ND_IDIV:
    case ND_EQ:
    case ND_NEQ:
    case ND_LT:
    case ND_LTE:
    case ND_LOGICAL_OR:
    case ND_LOGICAL_AND:
    case ND_INCLUSIVE_OR:
    case ND_EXCLUSIVE_OR:
    case ND_AND:
    case ND_LEFT_SHIFT:
    case ND_RIGHT_SHIFT:
    {
      if (node->lhs->kind != ND_NUM || node->rhs->kind != ND_NUM)
        break;
      long long lhs_val = node->lhs->num_val;
      long long rhs_val = node->rhs->num_val;
      long long result_val;
      switch (node->kind)
      {
        case ND_ADD:
          if ((rhs_val > 0 && lhs_val > LLONG_MAX - rhs_val) ||
              (rhs_val < 0 && lhs_val < LLONG_MIN - rhs_val))
            break;
          result_val = lhs_val + rhs_val;
          break;
        case ND_SUB:
          if ((rhs_val > 0 && lhs_val < LLONG_MIN + rhs_val) ||
              (rhs_val < 0 && lhs_val > LLONG_MAX + rhs_val))
            break;
          result_val = lhs_val - rhs_val;
          break;
        case ND_MUL:
          if (lhs_val > LLONG_MAX / rhs_val)
            break;
          result_val = lhs_val * rhs_val;
          break;
        case ND_DIV:
        case ND_IDIV:
          if (rhs_val == 0)
            break;
          result_val = lhs_val / rhs_val;
          break;
        case ND_EQ: result_val = lhs_val == rhs_val; break;
        case ND_NEQ: result_val = lhs_val != rhs_val; break;
        case ND_LT: result_val = lhs_val < rhs_val; break;
        case ND_LTE: result_val = lhs_val <= rhs_val; break;
        case ND_LOGICAL_OR: result_val = lhs_val || rhs_val; break;
        case ND_LOGICAL_AND: result_val = lhs_val && rhs_val; break;
        case ND_INCLUSIVE_OR: result_val = lhs_val | rhs_val; break;
        case ND_EXCLUSIVE_OR: result_val = lhs_val ^ rhs_val; break;
        case ND_AND: result_val = lhs_val & rhs_val; break;
        case ND_LEFT_SHIFT: result_val = lhs_val << rhs_val; break;
        case ND_RIGHT_SHIFT: result_val = lhs_val >> rhs_val; break;
        default: break;
      }
      node->kind = ND_NUM;
      node->num_val = result_val;
      node->lhs = NULL;
      node->rhs = NULL;
    }
    break;
    case ND_LOGICAL_NOT:
    case ND_NOT:
    case ND_UNARY_PLUS:
    case ND_UNARY_MINUS:
    {
      if (node->lhs->kind != ND_NUM)
        break;
      long long defare_val = node->lhs->num_val;
      long long result_val;
      switch (node->kind)
      {
        case ND_LOGICAL_NOT: result_val = !defare_val; break;
        case ND_NOT: result_val = ~defare_val; break;
        case ND_UNARY_PLUS: result_val = defare_val; break;
        case ND_UNARY_MINUS: result_val = -defare_val; break;
        default: break;
      }
      node->kind = ND_NUM;
      node->num_val = result_val;
      node->lhs = NULL;
      node->rhs = NULL;
    }
    break;
    default: break;
  }

  return node;
}

FuncBlock* optimizer(FuncBlock* func)
{  // constant folding
  for (FuncBlock* pointer = func; pointer; pointer = pointer->next)
  {
    Node* node = pointer->node;
    if (node && node->kind == ND_FUNCDEF)
    {
      for (NDBlock* tmp = node->func.stmt; tmp; tmp = tmp->next)
        tmp->node = constant_folding(tmp->node);
    }
  }
  return func;
}