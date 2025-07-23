// ------------------------------------------------------------------------------------
// semantic analyzer
// ------------------------------------------------------------------------------------

#include "include/analyzer.h"

#ifdef SELF_HOST
#include "test/compiler_header.h"
#else
#include <stdlib.h>
#include <string.h>
#endif

#include "include/debug.h"
#include "include/error.h"
#include "include/generator.h"
#include "include/offset.h"
#include "include/parser.h"
#include "include/type.h"

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
  if (!lhs || !rhs)
    unreachable();
  if (is_pointer_type(lhs) && is_pointer_type(rhs))
    return is_equal_type(lhs->ptr_to, rhs->ptr_to);
  if (lhs->type != rhs->type)
    return false;
  if (is_integer_type(lhs))
    return lhs->is_signed == rhs->is_signed;
  switch (lhs->type)
  {
    case TYPE_VOID:
    case TYPE_STR: return true;
    case TYPE_ENUM:
    case TYPE_STRUCT: return lhs->type_num == rhs->type_num;
    case TYPE_FUNC:
      if (vector_size(lhs->param_list) != vector_size(rhs->param_list))
        return false;
      for (size_t i = 1; i <= vector_size(lhs->param_list); i++)
        if (!is_equal_type(vector_peek_at(lhs->param_list, i),
                           vector_peek_at(rhs->param_list, i)))
          return false;
      return true;
    case TYPE_VARIABLES: return true;
    default: break;
  }
  unreachable();
  return false;
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
  if (lhs->type == TYPE_INT || lhs->type == TYPE_ENUM ||
      rhs->type == TYPE_INT || rhs->type == TYPE_ENUM)
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

Type *promote_integer(Type *type, Token *token)
{
  if (!is_integer_type(type))
    error_at(token->str, token->len, "Operand must be an integer type.");

  if (type->type == TYPE_BOOL || type->type == TYPE_CHAR ||
      type->type == TYPE_SHORT)
  {
    Type *promoted_type = alloc_type(TYPE_INT);
    promoted_type->is_signed = true;
    return promoted_type;
  }
  return type;
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
    error_at(node->token->str, node->token->len, "Invalid case statement.");
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

void add_type_for_assignment(Node *node)
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
    if ((lhs_type->type == TYPE_PTR && lhs_type->ptr_to->type == TYPE_VOID) ||
        (rhs_type->type == TYPE_PTR && rhs_type->ptr_to->type == TYPE_VOID))
    {
      node->type = lhs_type;
      return;
    }
  }

  // T* = 0
  if (is_pointer_type(lhs_type) && rhs_type->type == TYPE_INT &&
      node->rhs->kind == ND_NUM && node->rhs->num_val == 0)
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
           "Cannot convert both sides of '=' types.");
}

bool check_initializer_type(Type *type, Node *init_list, bool is_root)
{
  if (init_list->kind == ND_INITIALIZER)
  {
    init_list->type = type;
    const size_t initializer_size =
        vector_size(init_list->initialize.init_list);
    if (type->type != TYPE_ARRAY)
      error_at(init_list->token->str, init_list->token->len,
               "array initializer requires an array type");
    else if (type->size != 0)
    {
      if (type->size < initializer_size)
        error_at(init_list->token->str, init_list->token->len,
                 "too many elements in initializer");
    }
    else if (is_root)
      type->size = initializer_size;
    else
      error_at(init_list->token->str, init_list->token->len,
               "array size is not specified");

    for (size_t i = 1; i <= vector_size(init_list->initialize.init_list); i++)
    {
      Node *child = vector_peek_at(init_list->initialize.init_list, i);
      if (!(check_initializer_type(type->ptr_to, child, false)))
        return false;
    }
    return true;
  }
  Node *tmp = calloc(1, sizeof(Node));
  tmp->type = type;
  add_type_for_assignment(new_node(ND_ASSIGN, tmp, init_list, NULL));
  return true;
}

void add_type_internal(Node *node)
{
  switch (node->kind)
  {
    case ND_NOP: return;
    case ND_ADD:
    {
      if (is_pointer_type(node->lhs->type) && is_pointer_type(node->rhs->type))
        error_at(node->token->str, node->token->len,
                 "Invalid operands to binary +");

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
        n->type = alloc_type(TYPE_LONG);  // sizeof(TYPE_LONG) == sizeof(void*)
        node =
            new_node(ND_DIV, n, new_node_num(size_of(node->lhs->type->ptr_to)),
                     node->token);
        node->type = alloc_type(TYPE_LONG);
        node->type->is_signed = false;
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
                 "Invalid operands to binary -");
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
                 "Operands for '%s' must be integer types.",
                 (node->kind == ND_MUL ? "*" : "/"));
      node->type = implicit_type_conversion(node->lhs->type, node->rhs->type);
      return;
    }
    case ND_EQ:
    case ND_NEQ:
    case ND_LT:
    case ND_LTE: node->type = alloc_type(TYPE_BOOL); return;
    case ND_ASSIGN: add_type_for_assignment(node); return;
    case ND_ADDR:
    {
      Type *ptr = alloc_type(TYPE_PTR);
      ptr->ptr_to = node->lhs->type;
      node->type = ptr;
      return;
    }
    case ND_DEREF:
      if (!is_pointer_type(node->lhs->type))
        error_at(node->token->str, node->token->len, "Invalid dereference.");
      if (!node->lhs->type->ptr_to)
        error_at(node->token->str, node->token->len,
                 "Cannot dereference a void pointer.");
      node->type = node->lhs->type->ptr_to;
      size_of(node->type);  // check if it's a valid type
      return;
    case ND_UNARY_PLUS:
    case ND_UNARY_MINUS:
    case ND_LOGICAL_NOT:
    case ND_NOT:
      if (node->lhs->type->type == TYPE_ARRAY ||
          node->lhs->type->type == TYPE_PTR)
      {
        if (node->kind == ND_UNARY_MINUS || node->kind == ND_NOT)
          error_at(node->token->str, node->token->len,
                   "ND_UNARY_MINUS or ND_NOT must be an integer type.");
        node->type = alloc_type(TYPE_INT);
      }
      else
        node->type = promote_integer(node->lhs->type, node->token);
      return;
    case ND_PREINCREMENT:
    case ND_PREDECREMENT:
    case ND_POSTINCREMENT:
    case ND_POSTDECREMENT:
      if (!is_integer_type(node->lhs->type) &&
          !is_pointer_type(node->lhs->type))
        error_at(node->token->str, node->token->len,
                 "Operand must be integer or pointer for inc/dec.");
      node->type = node->lhs->type;
      if (node->lhs->type->ptr_to)
        node->num_val = size_of(node->lhs->type->ptr_to);
      return;
    case ND_FUNCDEF: node->type = alloc_type(TYPE_VOID); return;
    case ND_FUNCCALL:
    {
      if (!node->type)  // unknown function
        node->type = alloc_type(TYPE_INT);
      else
      {
        Type *last_param = vector_peek(node->type->param_list);
        bool is_variadic = last_param->type == TYPE_VARIABLES;

        size_t num_declared_params = vector_size(node->type->param_list) - 1;
        size_t num_args = vector_size(node->func.expr);

        if (is_variadic)
        {
          if (num_args < num_declared_params - 1)
          {
            error_at(node->token->str, node->token->len,
                     "Too few arguments to function call.");
          }
        }
        else
        {
          if (num_args != num_declared_params)
          {
            error_at(node->token->str, node->token->len,
                     "Invalid number of arguments to function call.");
          }
        }

        size_t fixed_param_count =
            is_variadic ? num_declared_params - 1 : num_declared_params;

        for (size_t i = 1; i <= fixed_param_count; i++)
        {
          Node *arg_node = vector_peek_at(node->func.expr, i);
          Node *param_node = calloc(1, sizeof(Node));
          param_node->type = vector_peek_at(node->type->param_list, i + 1);

          Node *assign_node =
              new_node(ND_ASSIGN, param_node, arg_node, arg_node->token);
          add_type_for_assignment(assign_node);

          if (assign_node->rhs != arg_node)
            vector_replace_at(node->func.expr, i, assign_node->rhs);

          free(param_node);
          free(assign_node);
        }

        node->type = vector_peek_at(node->type->param_list, 1);
      }
      return;
    }
    case ND_BUILTINFUNC:
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
      Type *lhs_type = node->control.ternary_child->type;
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
               "Mismatched types in ternary operator.");
      return;
    }
    case ND_LOGICAL_OR:
    case ND_LOGICAL_AND:
    {
      Type *lhs = node->lhs->type;
      Type *rhs = node->rhs->type;
      if (!is_integer_type(lhs) && !is_pointer_type(lhs))
        error_at(node->lhs->token->str, node->lhs->token->len,
                 "Scalar type required.");
      if (!is_integer_type(rhs) && !is_pointer_type(rhs))
        error_at(node->rhs->token->str, node->rhs->token->len,
                 "Scalar type required.");
      node->type = alloc_type(TYPE_BOOL);
      return;
    }
    case ND_INCLUSIVE_OR:
    case ND_EXCLUSIVE_OR:
    case ND_AND:
      if (!is_integer_type(node->lhs->type) ||
          !is_integer_type(node->rhs->type))
        error_at(node->token->str, node->token->len,
                 "Operands for bitwise op must be integer types.");
      node->type = implicit_type_conversion(node->lhs->type, node->rhs->type);
      return;
    case ND_LEFT_SHIFT:
    case ND_RIGHT_SHIFT:
      if (!is_integer_type(node->lhs->type) ||
          !is_integer_type(node->rhs->type))
        error_at(node->token->str, node->token->len,
                 "Operands for shift op must be integer types.");
      node->type = node->lhs->type;
      return;
    case ND_ASSIGNMENT:
      node->kind = ND_ASSIGN;
      node->lhs = node->rhs->lhs;
      add_type_internal(node);
      return;
    case ND_VAR: node->type = node->variable.var->type; return;
    case ND_ARRAY:
      if (node->lhs->type->type != TYPE_ARRAY &&
          node->lhs->type->type != TYPE_PTR)
        error_at(node->lhs->token->str, node->lhs->token->len, "invalid array");
      node->type = node->lhs->type->ptr_to;

      node->kind = ND_DEREF;
      node->lhs = new_node(ND_ADD, node->lhs, node->rhs, node->token);
      node->rhs = NULL;
      if (node->variable.is_array_top)
        add_type(node);  // add_type internal should call only one time
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
    case ND_NUM:
    {
      Type *t = alloc_type(TYPE_INT);
      t->is_signed = true;
      node->type = t;
      return;
    }
    case ND_BLOCK: node->type = alloc_type(TYPE_VOID); return;
    case ND_DISCARD_EXPR: node->type = alloc_type(TYPE_VOID); return;
    case ND_STRING: node->type = alloc_type(TYPE_STR); return;
    case ND_GOTO:
    case ND_LABEL:
    case ND_SWITCH: node->type = alloc_type(TYPE_VOID); return;
    case ND_CASE:
    {
      size_t num;
      node->jump.switch_name = switch_add(node, &num);
      node->jump.case_num = num;
      node->type = alloc_type(TYPE_VOID);
      return;
    }
    case ND_COMMA: node->type = node->rhs->type; return;
    case ND_INITIALIZER:
      if (node->initialize.is_top_initializer)
      {  // check if the assigned types are correct
        node->type = node->initialize.assigned->type;
        if (node->initialize.assigned->kind != ND_VAR &&
            node->initialize.assigned->type->type == TYPE_ARRAY)
          error_at(node->initialize.assigned->token->str,
                   node->initialize.assigned->token->len,
                   "invalid initializer");
        if (!(check_initializer_type(node->initialize.assigned->type, node,
                                     true)))
          error_at(node->token->str, node->token->len, "invalid initializer");
      }
      break;
    case ND_CAST: node->type = node->rhs->type; return;
    default: unreachable();
  }
}

void add_type(Node *node)
{
  if (!node)
    return;
  if (node->kind == ND_SWITCH)
    node->control.case_list = switch_new(node->control.label);
  if (node->lhs)
    add_type(node->lhs);
  if (node->rhs)
    add_type(node->rhs);
  if (node->kind == ND_TERNARY)
    add_type(node->control.ternary_child);
  if (node->kind == ND_IF || node->kind == ND_ELIF || node->kind == ND_FOR ||
      node->kind == ND_WHILE || node->kind == ND_DO || node->kind == ND_SWITCH)
  {
    add_type(node->control.condition);
    add_type(node->control.true_code);
    add_type(node->control.false_code);
    add_type(node->control.init);
    add_type(node->control.update);
  }
  if (node->kind == ND_LABEL || node->kind == ND_CASE)
    add_type(node->jump.statement_child);
  if (node->kind == ND_FUNCCALL || node->kind == ND_FUNCDEF)
    for (size_t i = 1; i <= vector_size(node->func.expr); i++)
      add_type(vector_peek_at(node->func.expr, i));
  if (node->kind == ND_BLOCK || node->kind == ND_FUNCDEF)
    for (NDBlock *tmp = node->func.stmt; tmp; tmp = tmp->next)
      add_type(tmp->node);
  if (node->kind == ND_INITIALIZER)
    for (size_t i = 1; i <= vector_size(node->initialize.init_list); i++)
      add_type(vector_peek_at(node->initialize.init_list, i));
  if (node->kind == ND_SWITCH)
    switch_end();

  add_type_internal(node);
}

Node *analyze_type(Node *node)
{
  if (!node)
    return NULL;
  if (node->lhs)
    node->lhs = analyze_type(node->lhs);
  if (node->rhs)
    node->rhs = analyze_type(node->rhs);
  if (node->kind == ND_TERNARY)
    node->control.ternary_child = analyze_type(node->control.ternary_child);
  if (node->kind == ND_IF || node->kind == ND_ELIF || node->kind == ND_FOR ||
      node->kind == ND_WHILE || node->kind == ND_DO)
  {
    node->control.condition = analyze_type(node->control.condition);
    node->control.true_code = analyze_type(node->control.true_code);
    node->control.false_code = analyze_type(node->control.false_code);
    node->control.init = analyze_type(node->control.init);
    node->control.update = analyze_type(node->control.update);
  }
  if (node->kind == ND_LABEL || node->kind == ND_CASE)
    node->jump.statement_child = analyze_type(node->jump.statement_child);
  if (node->kind == ND_FUNCCALL || node->kind == ND_FUNCDEF)
    for (size_t i = 1; i <= vector_size(node->func.expr); i++)
      vector_replace_at(node->func.expr, i,
                        analyze_type(vector_peek_at(node->func.expr, i)));
  if (node->kind == ND_BLOCK)
  {
    offset_enter_nest();
    for (NDBlock *tmp = node->func.stmt; tmp; tmp = tmp->next)
      tmp->node = analyze_type(tmp->node);
    offset_exit_nest();
  }

  switch (node->kind)
  {
    case ND_DEREF:
      // When the dereferenced type is a multidimensional array
      if (node->type->type == TYPE_ARRAY)
      {
        node->lhs->type = node->type;
        memcpy(node, node->lhs, sizeof(Node));
      }
      break;
    case ND_SIZEOF:
      node->kind = ND_NUM;
      node->num_val = size_of(node->lhs->type);
      break;
    case ND_VAR:
      if (node->variable.var->is_local &&
          !(node->variable.var->storage_class_specifier & 1 << 1) &&
          node->variable.is_new_var)
        node->variable.var->offset = calculate_offset(size_of(node->type));
      break;
    case ND_NUM:
      if (node->type->type == TYPE_PTR || node->type->type == TYPE_ARRAY)
      {
        node->num_val = node->num_val * size_of(node->type->ptr_to);
      }
      break;
    case ND_STRING:
      // TODO: Behavior is different for ND_ARRAY
      node->literal_name = add_string_literal(node->token);
      break;
    default: break;
  }
  return node;
}

FuncBlock *analyzer(FuncBlock *funcblock)
{
  pr_debug("Start analyze");
  // Apply types to nodes that are not variables or numbers, such as ND_ASSIGN
  for (FuncBlock *pointer = funcblock; pointer; pointer = pointer->next)
  {
    Node *node = pointer->node;
    if (!node)
      continue;
    if (node->kind == ND_FUNCDEF)
    {
      for (size_t i = 1; i <= vector_size(node->func.expr); i++)
        add_type(vector_peek_at(node->func.expr, i));
      for (NDBlock *tmp = node->func.stmt; tmp; tmp = tmp->next)
        add_type(tmp->node);
    }
    else if (node->kind == ND_VAR)
    {
      if (node->variable.var->is_local)
        error_exit("Failed to parse correctly.");
      add_type(node);
    }
    else if (node->kind == ND_BUILTINFUNC)
      node->type = alloc_type(TYPE_VOID);
    else if (node->kind != ND_NOP)
      add_type(node);
  }
#ifdef DEBUG
  print_parse_result(funcblock);
#endif
  // Calculate offsets based on the types assigned by add_type
  for (FuncBlock *pointer = funcblock; pointer; pointer = pointer->next)
  {
    Node *node = pointer->node;
    if (!node)
      continue;
    if (node->kind == ND_FUNCDEF)
    {
      init_nest();
      for (size_t i = 1; i <= vector_size(node->func.expr); i++)
        vector_replace_at(node->func.expr, i,
                          analyze_type(vector_peek_at(node->func.expr, i)));
      for (NDBlock *tmp = node->func.stmt; tmp; tmp = tmp->next)
        tmp->node = analyze_type(tmp->node);
      // Align stacksize to 8-byte units
      size_t max_stacksize = get_max_offset();
      pointer->stacksize = (max_stacksize + 7) / 8 * 8;
    }
    else if (node->kind != ND_NOP && node->kind != ND_BUILTINFUNC)
    {
      pointer->node = analyze_type(node);
      node = pointer->node;
      // global variables initializer only allows constant value
      if (node->kind != ND_ASSIGN && node->kind != ND_VAR)
        error_at(node->token->str, node->token->len,
                 "invalid global variable initializer");
      if (node->kind == ND_ASSIGN)
      {
        switch (node->rhs->kind)
        {
          case ND_NUM:
          case ND_STRING: break;
          case ND_ADDR:
            if (node->rhs->lhs->kind != ND_VAR)
              error_at(node->rhs->lhs->token->str, node->rhs->lhs->token->len,
                       "invalid global variable initializer");
            break;
          case ND_EVAL:
            if (node->rhs->lhs->kind != ND_NUM)
              error_at(node->rhs->lhs->token->str, node->rhs->lhs->token->len,
                       "invalid global variable initializer");
            node->rhs = node->rhs->lhs;
            node->rhs->num_val = node->rhs->num_val ? 1 : 0;
            break;
          default:
            error_at(node->rhs->token->str, node->rhs->token->len,
                     "invalid global variable initializer");
        }
      }
    }
  }
#ifdef DEBUG
  print_parse_result(funcblock);
#endif
  return funcblock;
}