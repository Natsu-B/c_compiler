// ------------------------------------------------------------------------------------
// parser
// ------------------------------------------------------------------------------------

#include "include/parser.h"

#ifdef SELF_HOST
#include "test/compiler_header.h"
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

#include "include/builtin.h"
#include "include/debug.h"
#include "include/error.h"
#include "include/eval_constant.h"
#include "include/tokenizer.h"
#include "include/type.h"
#include "include/vector.h"

Type *abstract_declarator(Type *type);
Type *direct_abstract_declarator(Type *type);
Node *external_declaration();
Node *function_declaration();
Node *declaration(Type *type, bool is_external_declaration,
                  uint8_t storage_class_specifier);
Node *init_declarator(Type *type, uint8_t storage_class_specifier);
Node *declarator_no_side_effect(Type **type, uint8_t storage_class_specifier);
Node *declarator(Type *type, uint8_t storage_class_specifier);

Type *pointer(Type *type);
Node *type_name();
Node *initializer(Node *assigned);
Node *block_item();
Node *statement();
Node *expression();
Node *assignment_expression();
Node *constant_expression();
Node *conditional_expression();
Node *logical_OR_expression();
Node *logical_AND_expression();
Node *inclusive_OR_expression();
Node *exclusive_OR_expression();
Node *AND_expression();
Node *equality_expression();
Node *relational_expression();
Node *shift_expression();
Node *additive_expression();
Node *multiplicative_expression();
Node *cast_expression();
Node *unary_expression();
Node *postfix_expression();
Node *primary_expression();

FuncBlock head;

FuncBlock *get_funcblock_head()
{
  return head.next;
}

const char *nodekindlist[ND_END] = {NodeKindTable};

// Vector to hold GTLabel for the same nest
// When the nest deepens, a new vector is pushed, and when it becomes shallow,
// it is popped
Vector *label_list;
// Current function name and name being parsed
char *program_name;
int program_name_len;

void new_nest_label()
{
  if (!label_list)
    label_list = vector_new();
  vector_push(label_list, vector_new());
}

void exit_nest_label()
{
  vector_pop(label_list);
}

GTLabel *generate_label_name(NodeKind kind)
{
  static size_t label_name_counter;
  size_t namesize = program_name_len + 6;
  GTLabel *next = calloc(1, sizeof(GTLabel));
  next->kind = kind;
  char *mangle_name = malloc(namesize);
  int size = snprintf(mangle_name, namesize, "_%lu_%.*s", label_name_counter++,
                      program_name_len, program_name);
  if (size < 0 || (size_t)size >= namesize)
    unreachable();
  next->len = size;
  next->name = mangle_name;
  vector_push(vector_peek(label_list), next);
  return next;
}

// Function to find the jump target for break/continue
// If type is 1, it's for continue; if type is 2, it's for break.
char *find_jmp_target(size_t type)
{
  // Function to find the jump target for break/continue
  // If type is 1, it's for continue; if type is 2, it's for break.
  // Search for while/for loops, starting from the deepest nested one.
  for (size_t i = vector_size(label_list); i >= 1; i--)
  {
    for (size_t j = 1; j <= vector_size(vector_peek_at(label_list, i)); j++)
    {
      GTLabel *labeled_loop = vector_peek_at(vector_peek_at(label_list, i), j);
      if (!labeled_loop || (labeled_loop->kind == ND_SWITCH && type == 1))
        continue;
      if (labeled_loop->kind == ND_WHILE || labeled_loop->kind == ND_FOR ||
          labeled_loop->kind == ND_DO || labeled_loop->kind == ND_SWITCH)
      {
        char *label_name =
            malloc(labeled_loop->len + 14 /*.Lbeginswitch + null terminator*/);
        size_t printed = 0;
        switch (type)
        {
          case 1:
            strcpy(label_name, ".Lbegin");
            printed = 7;
            break;
          case 2:
            strcpy(label_name, ".Lend");
            printed = 5;
            break;
          default: unreachable(); break;
        }
        switch (labeled_loop->kind)
        {
          case ND_WHILE:
            strcpy(label_name + printed, "while");
            printed += 5;
            break;
          case ND_DO:
            strcpy(label_name + printed, "do");
            printed += 2;
            break;
          case ND_FOR:
            strcpy(label_name + printed, "for");
            printed += 3;
            break;
          case ND_SWITCH:
            strcpy(label_name + printed, "switch");
            printed += 6;
            break;
          default: unreachable(); break;
        }

        strncpy(label_name + printed, labeled_loop->name, labeled_loop->len);
        *(label_name + printed + labeled_loop->len) = '\0';
        return label_name;
      }
    }
  }
  error_at(get_old_token()->str, get_old_token()->len,
           "invalid break; continue; statement");
  return NULL;
}

// Set the goto label to .Lgoto_YYY_XXX (YYY is the label name, XXX is the
// function name)
char *mangle_goto_label(Token *token)
{
  char *label_name = malloc(token->len + program_name_len + 9);
  strcpy(label_name, ".Lgoto_");
  strncpy(label_name + 7, token->str, token->len);
  *(label_name + 7 + token->len) = '_';
  strncpy(label_name + 7 + token->len + 1, program_name, program_name_len);
  return label_name;
}

Node *new_node(NodeKind kind, Node *lhs, Node *rhs, Token *token)
{
  Node *node = calloc(1, sizeof(Node));
  node->kind = kind;
  node->lhs = lhs;
  node->rhs = rhs;
  node->token = token;

  return node;
}

Node *new_node_num(long long val)
{
  Node *node = calloc(1, sizeof(Node));
  node->kind = ND_NUM;
  node->num_val = val;
  Type *t = alloc_type(TYPE_INT);
  t->is_signed = true;
  node->type = t;
  return node;
}

void new_nest()
{
  new_nest_type();
  new_nest_label();
}

void exit_nest()
{
  exit_nest_type();
  exit_nest_label();
}

Vector *parameter_type_list(Vector **type_list, Type *type,
                            uint8_t storage_class_specifier)
{  // Creates a list of function argument nodes and consumes ")" to end.
  if (storage_class_specifier &
      ~(1 << 1 | 1 << 2))  // Functions are blocks except for static and extern
    error_exit("invalid storage specifier");
  if (type_list)
    vector_push(*type_list, type);
  Vector *list = vector_new();
  Token *old = get_token();
  if (consume("void", TK_IDENT) && consume(")", TK_RESERVED))
    return list;
  set_token(old);
  while (!consume(")", TK_RESERVED))
  {
    if (consume("...", TK_RESERVED))
    {
      vector_push(list,
                  new_node(ND_VARIABLE_ARGS, NULL, NULL, get_old_token()));
      if (type_list)
        vector_push(*type_list, alloc_type(TYPE_VARIABLES));
      expect(")", TK_RESERVED);
      break;
    }
    else
    {
      uint8_t storage_class_specifier = 0;
      Type *type = declaration_specifiers(&storage_class_specifier);
      if (!type || storage_class_specifier)
        error_at(get_token()->str, get_token()->len, "Invalid type.");
      Node *parameter =
          declarator_no_side_effect(&type, storage_class_specifier);
      vector_push(list, parameter);
      if (type_list)
        vector_push(*type_list, type);
      if (!consume(",", TK_RESERVED))
      {
        expect(")", TK_RESERVED);
        break;
      }
    }
  }
  return list;
}

// ------------------------------------------------------------------------------------
// parser
// ------------------------------------------------------------------------------------

FuncBlock *parser()
{
  pr_debug("Starting parser...");
  head.next = NULL;
  FuncBlock *pointer = &head;
  init_types();
  while (!at_eof())
  {
    FuncBlock *new = calloc(1, sizeof(FuncBlock));
    pointer->next = new;
    new->node = external_declaration();
    pointer = new;
  }
#ifdef DEBUG
  print_parse_result(head.next);
#endif
  pr_debug("Parsing complete.");
  return head.next;
}

Node *external_declaration()
{
#if DEBUG
  Token *t = get_token();
  pr_debug2("Current token: '%.*s'", (int)t->len, t->str);
#endif
  uint8_t storage_class_specifier = 0;
  Type *type = declaration_specifiers(&storage_class_specifier);
  return declaration(type, true, storage_class_specifier);
}

Node *declaration(Type *type, bool is_external_declaration,
                  uint8_t storage_class_specifier)
{
  Node *node = init_declarator(type, storage_class_specifier);
  if (node && consume(",", TK_RESERVED))
  {
    for (;;)
    {
      Token *old = get_old_token();
      Node *new = init_declarator(type, storage_class_specifier);
      if (!new)
        error_at(old->str, old->len, "invalid declarator");
      node = new_node(ND_DECLARATOR_LIST, node, new, old);
      if (!consume(",", TK_RESERVED))
        break;
    }
  }
  // funcdef
  if (is_external_declaration && node && node->kind == ND_FUNCDEF &&
      consume("{", TK_RESERVED))
  {
    program_name = node->token->str;
    program_name_len = node->token->len;
    if (!add_function_name(node->type->param_list, node->token,
                           storage_class_specifier, true))
      error_at(node->token->str, node->token->len,
               "invalid function definition");
    new_nest();
    for (size_t i = 1; i <= vector_size(node->func.expr); i++)
    {
      Node *param = vector_peek_at(node->func.expr, i);
      param->variable.var = add_variables(param->token, param->type, 0);
      param->variable.is_new_var = true;
    }
    NDBlock head;
    head.next = NULL;
    NDBlock *pointer = &head;

    while (!consume("}", TK_RESERVED))
    {
      NDBlock *next = calloc(1, sizeof(NDBlock));
      pointer->next = next;
      next->node = block_item();
      pointer = next;
    }
    node->func.stmt = head.next;
    node->func.storage_class_specifier = storage_class_specifier;
    exit_nest();
    return node;
  }
  else if (node && node->kind == ND_FUNCDEF)
    node->kind = ND_NOP;

  expect(";", TK_RESERVED);
  return node;
}

Node *init_declarator(Type *type, uint8_t storage_class_specifier)
{
  Node *node = declarator(type, storage_class_specifier);
  if (!node)
    return NULL;
  Token *old = get_token();
  if (node->kind != ND_FUNCDEF && consume("=", TK_RESERVED))
  {
    node = new_node(ND_ASSIGN, node, initializer(node), old);
    if (node->rhs->kind == ND_INITIALIZER)
    {
      if (node->lhs->type->type != TYPE_ARRAY)
        error_at(node->lhs->token->str, node->lhs->token->len,
                 "invalid initializer");
      if (!node->lhs->type->size)
        node->lhs->type->size = vector_size(node->rhs->initialize.init_list);
    }
  }
  return node;
}

bool consume_array(Type **type)
{
  if (consume("[", TK_RESERVED))
  {
    Type *new = alloc_type(TYPE_ARRAY);
    if (peek("]", TK_RESERVED))
      new->size = 0;
    else
      new->size = eval_constant_expression();
    expect("]", TK_RESERVED);
    consume_array(type);
    new->ptr_to = *type;
    *type = new;
    return true;
  }
  return false;
}

Node *declarator_internal(Type **type, Token *token,
                          uint8_t storage_class_specifier)
{
  if (!token)
  {
    if (consume("(", TK_RESERVED))
    {
      Node *result = declarator(*type, storage_class_specifier);
      expect(")", TK_RESERVED);
      return result;
    }
    return NULL;
  }
  if (peek("(", TK_RESERVED))
  {
    Node *node = NULL;
    if (token && is_builtin_function(&node, token, true))
      return node;
    consume("(", TK_RESERVED);
    node = calloc(1, sizeof(Node));
    node->kind = ND_FUNCDEF;
    node->token = token;
    Type *new = alloc_type(TYPE_FUNC);
    new->param_list = vector_new();
    node->type = new;
    node->func.expr =
        parameter_type_list(&new->param_list, *type, storage_class_specifier);
    node->func.storage_class_specifier = storage_class_specifier;
    if (!add_function_name(new->param_list, token, storage_class_specifier,
                           false))
      error_at(token->str, token->len, "invalid name");
    *type = new;
    return node;
  }
  else
    consume_array(type);

  Node *node = calloc(1, sizeof(Node));
  node->token = token;
  node->kind = ND_VAR;
  node->type = *type;
  return node;
}

Node *declarator_no_side_effect(Type **type, uint8_t storage_class_specifier)
{
  if (storage_class_specifier)
    error_exit("invalid storage class specifier");
  *type = pointer(*type);
  Token *token = consume_ident();
  return declarator_internal(type, token, storage_class_specifier);
}

Node *declarator(Type *type, uint8_t storage_class_specifier)
{
  type = pointer(type);
  Token *token = consume_ident();
  Node *node = declarator_internal(&type, token, storage_class_specifier);
  if (node && type && type->type != TYPE_FUNC)
  {
    Var *var = add_variables(token, type, storage_class_specifier);
    if (!var)
      return new_node(ND_NOP, NULL, NULL, NULL);
    node->variable.var = var;
    if (type)
      node->variable.is_new_var = true;
  }
  return node;
}

Type *pointer(Type *type)
{
  if (type)
    while (consume("*", TK_RESERVED))
    {
      Type *new = alloc_type(TYPE_PTR);
      new->ptr_to = type;
      type = new;
      while (consume("const", TK_IDENT) || consume("volatile", TK_IDENT) ||
             consume("restrict", TK_IDENT))
        ;
    }
  return type;
}

Node *type_name()
{
  uint8_t storage_class_specifier = 0;
  Type *type = declaration_specifiers(&storage_class_specifier);
  if (!type)
    return NULL;
  type = abstract_declarator(type);
  Node *node = calloc(1, sizeof(Node));
  node->kind = ND_TYPE_NAME;
  node->type = type;
  return node;
}

Type *abstract_declarator(Type *type)
{
  type = pointer(type);
  return direct_abstract_declarator(type);
}

// Allow even if it does not match direct_abstract_declarator
Type *direct_abstract_declarator(Type *type)
{
  bool was_grouped = false;
  Token *old = get_token();
  if (consume("(", TK_RESERVED))
  {
    if (!is_type_specifier(get_token()) && !peek(")", TK_RESERVED))
    {
      type = abstract_declarator(type);
      expect(")", TK_RESERVED);
      was_grouped = true;
    }
    else
      set_token(old);
  }

  for (;;)
  {
    if (consume_array(&type))
      continue;

    if (consume("(", TK_RESERVED))
    {
      if (was_grouped)
      {
        Type *tail = type;
        Type *parent_of_tail = NULL;
        while (tail->type == TYPE_ARRAY || tail->type == TYPE_PTR)
        {
          parent_of_tail = tail;
          tail = tail->ptr_to;
        }

        Type *func_type = alloc_type(TYPE_FUNC);
        func_type->param_list = vector_new();
        parameter_type_list(&func_type->param_list, tail, 0);

        if (parent_of_tail)
        {
          parent_of_tail->ptr_to = func_type;
        }
        else
        {
          type = func_type;
        }
      }
      else
      {
        Type *new = alloc_type(TYPE_FUNC);
        new->param_list = vector_new();
        parameter_type_list(&new->param_list, type, 0);
        type = new;
      }
    }
    break;
  }
  return type;
}

Node *initializer(Node *assigned)
{
  if (consume("{", TK_RESERVED))
  {
    Node *node = calloc(1, sizeof(Node));
    node->kind = ND_INITIALIZER;
    node->initialize.init_list = vector_new();
    node->initialize.is_top_initializer = true;
    node->initialize.assigned = assigned;
    node->token = get_old_token();

    while (!consume("}", TK_RESERVED))
    {
      Node *list = initializer(node);
      if (list->kind == ND_INITIALIZER)
        list->initialize.is_top_initializer = false;
      vector_push(node->initialize.init_list, list);
      if (!consume(",", TK_RESERVED) && !peek("}", TK_RESERVED))
        error_at(get_token()->str, get_token()->len, "invalid initializer");
    }
    return node;
  }
  return assignment_expression();
}

Node *block_item()
{
  uint8_t storage_class_specifier = 0;
  Type *type = declaration_specifiers(&storage_class_specifier);
  if (type)
    return declaration(type, false, storage_class_specifier);
  return statement();
}

Node *labeled_statement()
{
  Node *node = NULL;
  if (consume("case", TK_IDENT) || consume("default", TK_IDENT))
  {
    node = new_node(ND_CASE, NULL, NULL, get_old_token());
    if (get_old_token()->len == 4)  // case
    {
      node->jump.is_case = true;
      node->jump.constant_expression = eval_constant_expression();
    }
    else
      node->jump.is_case = false;
    expect(":", TK_RESERVED);
    node->jump.statement_child = statement();
    return node;
  }
  Token *token_ident = consume_token_if_next_matches(TK_IDENT, ':');
  if (token_ident)
  {
    expect(":", TK_RESERVED);
    node = new_node(ND_LABEL, NULL, NULL, token_ident);
    node->jump.label_name = mangle_goto_label(token_ident);
    node->jump.statement_child = statement();
    return node;
  }
  return node;
}

Node *statement()
{
#if DEBUG
  Token *t = get_token();
  pr_debug2("Current token: '%.*s'", (int)t->len, t->str);
#endif
  // Determine if it's a block: compound-statement
  if (consume("{", TK_RESERVED))
  {
    Node *node = calloc(1, sizeof(Node));
    NDBlock head;
    head.next = NULL;
    NDBlock *pointer = &head;
    node->kind = ND_BLOCK;
    new_nest();
    for (;;)
    {
      if (consume("}", TK_RESERVED))
        break;
      NDBlock *next = calloc(1, sizeof(NDBlock));
      pointer->next = next;
      next->node = block_item();
      pointer = next;
    }
    exit_nest();
    node->func.stmt = head.next;
    return node;
  }

  // selection-statement
  // Determine if it's an if statement and if it has an else clause
  if (consume("if", TK_IDENT))
  {
    Node *node = calloc(1, sizeof(Node));
    new_nest();
    node->control.label = generate_label_name(ND_IF);
    expect("(", TK_RESERVED);
    node->control.condition = expression();
    expect(")", TK_RESERVED);
    node->control.true_code = statement();
    if (consume("else", TK_IDENT))
    {
      node->kind = ND_ELIF;
      node->control.false_code = statement();
    }
    else
    {
      node->kind = ND_IF;
    }
    exit_nest();
    return node;
  }
  if (consume("switch", TK_IDENT))
  {
    Node *node = calloc(1, sizeof(Node));
    node->kind = ND_SWITCH;
    new_nest();
    expect("(", TK_RESERVED);
    node->control.label = generate_label_name(ND_SWITCH);
    node->control.condition = expression();
    expect(")", TK_RESERVED);
    node->control.true_code = statement();
    exit_nest();
    return node;
  }

  // iteration-statement
  // Determine if it's a while loop
  if (consume("while", TK_IDENT))
  {
    Node *node = calloc(1, sizeof(Node));
    node->kind = ND_WHILE;
    new_nest();
    node->control.label = generate_label_name(ND_WHILE);
    expect("(", TK_RESERVED);
    node->control.condition = expression();
    expect(")", TK_RESERVED);
    node->control.true_code = statement();
    exit_nest();
    return node;
  }
  // Determine if it's a do-while loop
  if (consume("do", TK_IDENT))
  {
    new_nest();
    Node *node = calloc(1, sizeof(Node));
    node->control.label = generate_label_name(ND_DO);
    node->control.true_code = statement();
    node->kind = ND_DO;
    expect("while", TK_IDENT);
    expect("(", TK_RESERVED);
    node->control.condition = expression();
    expect(")", TK_RESERVED);
    expect(";", TK_RESERVED);
    exit_nest();
    return node;
  }
  // Determine if it's a for loop
  if (consume("for", TK_IDENT))
  {
    Node *node = calloc(1, sizeof(Node));
    node->kind = ND_FOR;
    new_nest();
    node->control.label = generate_label_name(ND_FOR);
    expect("(", TK_RESERVED);
    // Whether it is a declaration
    uint8_t storage_class_specifier = 0;
    Type *type = declaration_specifiers(&storage_class_specifier);
    if (type)
      node->control.init = declaration(type, false, storage_class_specifier);
    else if (!consume(";", TK_RESERVED))
    {
      Node *new = calloc(1, sizeof(Node));
      node->control.init = new;
      new->kind = ND_DISCARD_EXPR;
      new->lhs = expression();
      expect(";", TK_RESERVED);
    }
    if (!consume(";", TK_RESERVED))
    {
      node->control.condition = expression();
      expect(";", TK_RESERVED);
    }
    if (!consume(")", TK_RESERVED))
    {
      Node *new = calloc(1, sizeof(Node));
      node->control.update = new;
      new->kind = ND_DISCARD_EXPR;
      new->lhs = expression();
      expect(")", TK_RESERVED);
    }
    node->control.true_code = statement();
    exit_nest();
    return node;
  }

  Node *node = labeled_statement();
  if (node)
    return node;
  if (consume("return", TK_IDENT))
    node = new_node(ND_RETURN, expression(), NULL, get_old_token());
  else if (consume("continue", TK_IDENT))
  {
    node = new_node(ND_GOTO, NULL, NULL, get_old_token());
    node->jump.label_name = find_jmp_target(1);
  }
  else if (consume("break", TK_IDENT))
  {
    node = new_node(ND_GOTO, NULL, NULL, get_old_token());
    node->jump.label_name = find_jmp_target(2);
  }
  else if (consume("goto", TK_IDENT))
  {
    node = new_node(ND_GOTO, NULL, NULL, get_old_token());
    node->jump.label_name = mangle_goto_label(expect_ident());
  }
  else
    node = new_node(ND_DISCARD_EXPR, expression(), NULL, get_token());
  expect(";", TK_RESERVED);
  return node;
}

Node *expression()
{
#if DEBUG
  Token *t = get_token();
  pr_debug2("Current token: '%.*s'", (int)t->len, t->str);
#endif
  Node *node = assignment_expression();
  while (consume(",", TK_RESERVED))
  {
    Token *old = get_old_token();
    node = new_node(ND_COMMA, node, assignment_expression(), old);
  }
  return node;
}

Node *assignment_expression()
{
#if DEBUG
  Token *t = get_token();
  pr_debug2("Current token: '%.*s'", (int)t->len, t->str);
#endif
  Node *node = conditional_expression();
  Token *old = get_token();
  if (consume("=", TK_RESERVED))
    node = new_node(ND_ASSIGN, node, assignment_expression(), get_old_token());
  else if (consume("*=", TK_RESERVED))
    node = new_node(ND_ASSIGNMENT, NULL,
                    new_node(ND_MUL, node, assignment_expression(), old), old);
  else if (consume("/=", TK_RESERVED))
    node = new_node(ND_ASSIGNMENT, NULL,
                    new_node(ND_DIV, node, assignment_expression(), old), old);
  else if (consume("%=", TK_RESERVED))
    node = new_node(ND_ASSIGNMENT, NULL,
                    new_node(ND_IDIV, node, assignment_expression(), old), old);
  else if (consume("+=", TK_RESERVED))
    node = new_node(ND_ASSIGNMENT, NULL,
                    new_node(ND_ADD, node, assignment_expression(), old), old);
  else if (consume("-=", TK_RESERVED))
    node = new_node(ND_ASSIGNMENT, NULL,
                    new_node(ND_SUB, node, assignment_expression(), old), old);
  else if (consume("<<=", TK_RESERVED))
    node = new_node(ND_ASSIGNMENT, NULL,
                    new_node(ND_LEFT_SHIFT, node, assignment_expression(), old),
                    old);
  else if (consume(">>=", TK_RESERVED))
    node = new_node(
        ND_ASSIGNMENT, NULL,
        new_node(ND_RIGHT_SHIFT, node, assignment_expression(), old), old);
  else if (consume("&=", TK_RESERVED))
    node = new_node(ND_ASSIGNMENT, NULL,
                    new_node(ND_AND, node, assignment_expression(), old), old);
  else if (consume("^=", TK_RESERVED))
    node = new_node(
        ND_ASSIGNMENT, NULL,
        new_node(ND_EXCLUSIVE_OR, node, assignment_expression(), old), old);
  else if (consume("|=", TK_RESERVED))
    node = new_node(
        ND_ASSIGNMENT, NULL,
        new_node(ND_INCLUSIVE_OR, node, assignment_expression(), old), old);
  return node;
}

Node *constant_expression()
{
  return conditional_expression();
}

Node *conditional_expression()
{
  Node *node = logical_OR_expression();
  if (consume("?", TK_RESERVED))
  {
    Token *old = get_old_token();
    Node *chs = expression();
    expect(":", TK_RESERVED);
    node = new_node(ND_TERNARY, node, expression(), old);
    node->control.label = generate_label_name(ND_TERNARY);
    node->control.ternary_child = chs;
  }
  return node;
}

Node *logical_OR_expression()
{
  Node *node = logical_AND_expression();
  while (consume("||", TK_RESERVED))
  {
    Token *old = get_old_token();
    node = new_node(ND_LOGICAL_OR, node, logical_AND_expression(), old);
    node->control.label = generate_label_name(ND_LOGICAL_OR);
  }
  return node;
}

Node *logical_AND_expression()
{
  Node *node = inclusive_OR_expression();
  while (consume("&&", TK_RESERVED))
  {
    Token *old = get_old_token();
    node = new_node(ND_LOGICAL_AND, node, equality_expression(), old);
    node->control.label = generate_label_name(ND_LOGICAL_AND);
  }
  return node;
}

Node *inclusive_OR_expression()
{
  Node *node = exclusive_OR_expression();
  while (consume("|", TK_RESERVED))
  {
    Token *old = get_old_token();
    node = new_node(ND_INCLUSIVE_OR, node, exclusive_OR_expression(), old);
  }
  return node;
}

Node *exclusive_OR_expression()
{
  Node *node = AND_expression();
  while (consume("^", TK_RESERVED))
  {
    Token *old = get_old_token();
    node = new_node(ND_EXCLUSIVE_OR, node, AND_expression(), old);
  }
  return node;
}

Node *AND_expression()
{
  Node *node = equality_expression();
  while (consume("&", TK_RESERVED))
  {
    Token *old = get_old_token();
    node = new_node(ND_AND, node, equality_expression(), old);
  }
  return node;
}

Node *equality_expression()
{
#if DEBUG
  Token *t = get_token();
  pr_debug2("Current token: '%.*s'", (int)t->len, t->str);
#endif
  Node *node = relational_expression();

  for (;;)
  {
    if (consume("==", TK_RESERVED))
      node = new_node(ND_EQ, node, relational_expression(), get_old_token());
    else if (consume("!=", TK_RESERVED))
      node = new_node(ND_NEQ, node, relational_expression(), get_old_token());
    else
      return node;
  }
}

Node *relational_expression()
{
#if DEBUG
  Token *t = get_token();
  pr_debug2("Current token: '%.*s'", (int)t->len, t->str);
#endif
  Node *node = shift_expression();
  for (;;)
  {
    if (consume("<=", TK_RESERVED))
      node = new_node(ND_LTE, node, shift_expression(), get_old_token());
    else if (consume("<", TK_RESERVED))
      node = new_node(ND_LT, node, shift_expression(), get_old_token());
    else if (consume(">=", TK_RESERVED))
      node = new_node(ND_LTE, shift_expression(), node, get_old_token());
    else if (consume(">", TK_RESERVED))
      node = new_node(ND_LT, shift_expression(), node, get_old_token());
    else
      return node;
  }
}

Node *shift_expression()
{
  Node *node = additive_expression();
  for (;;)
  {
    Token *token = get_token();
    if (consume(">>", TK_RESERVED))
      node = new_node(ND_RIGHT_SHIFT, node, additive_expression(), token);
    else if (consume("<<", TK_RESERVED))
      node = new_node(ND_LEFT_SHIFT, node, additive_expression(), token);
    else
      break;
  }
  return node;
}

Node *additive_expression()
{
#if DEBUG
  Token *t = get_token();
  pr_debug2("Current token: '%.*s'", (int)t->len, t->str);
#endif
  Node *node = multiplicative_expression();

  for (;;)
  {
    if (consume("+", TK_RESERVED))
    {
      node =
          new_node(ND_ADD, node, multiplicative_expression(), get_old_token());
    }
    else if (consume("-", TK_RESERVED))
    {
      node =
          new_node(ND_SUB, node, multiplicative_expression(), get_old_token());
    }
    else
      return node;
  }
}

Node *multiplicative_expression()
{
#if DEBUG
  Token *t = get_token();
  pr_debug2("Current token: '%.*s'", (int)t->len, t->str);
#endif
  Node *node = cast_expression();

  for (;;)
  {
    if (consume("*", TK_RESERVED))
      node = new_node(ND_MUL, node, cast_expression(), get_old_token());
    else if (consume("/", TK_RESERVED))
      node = new_node(ND_DIV, node, cast_expression(), get_old_token());
    else if (consume("%", TK_RESERVED))
      node = new_node(ND_IDIV, node, cast_expression(), get_old_token());
    else
      return node;
  }
}

Node *cast_expression()
{
  Token *tok = get_token();
  if (consume("(", TK_RESERVED) && is_type_specifier(get_token()))
  {
    Node *node = type_name();
    expect(")", TK_RESERVED);
    node = new_node(ND_CAST, cast_expression(), node, tok);
    return node;
  }
  set_token(tok);
  return unary_expression();
}

Node *unary_expression()
{
#if DEBUG
  Token *t = get_token();
  pr_debug2("Current token: '%.*s'", (int)t->len, t->str);
#endif
  if (consume("++", TK_RESERVED))
    return new_node(ND_PREINCREMENT, unary_expression(), NULL, get_old_token());
  if (consume("--", TK_RESERVED))
    return new_node(ND_PREDECREMENT, unary_expression(), NULL, get_old_token());
  if (consume("sizeof", TK_IDENT))
  {
    Token *old = get_old_token();
    Token *token = get_token();
    if (consume("(", TK_RESERVED) && is_type_specifier(get_token()))
    {
      Node *node = type_name();
      expect(")", TK_RESERVED);
      return new_node(ND_SIZEOF, node, NULL, old);
    }
    set_token(token);
    return new_node(ND_SIZEOF, unary_expression(), NULL, old);
  }
  if (consume("+", TK_RESERVED))
    return new_node(ND_UNARY_PLUS, cast_expression(), NULL, get_old_token());
  if (consume("-", TK_RESERVED))
    return new_node(ND_UNARY_MINUS, cast_expression(), NULL, get_old_token());
  if (consume("*", TK_RESERVED))
    return new_node(ND_DEREF, cast_expression(), NULL, get_old_token());
  if (consume("&", TK_RESERVED))
    return new_node(ND_ADDR, cast_expression(), NULL, get_old_token());
  if (consume("!", TK_RESERVED))
    return new_node(ND_LOGICAL_NOT, cast_expression(), NULL, get_old_token());
  if (consume("~", TK_RESERVED))
    return new_node(ND_NOT, cast_expression(), NULL, get_old_token());
  return postfix_expression();
}

Node *postfix_expression()
{
#if DEBUG
  Token *t = get_token();
  pr_debug2("Current token: '%.*s'", (int)t->len, t->str);
#endif
  Node *node = primary_expression();
  for (;;)
  {
    Token *old_token = get_old_token();
    if (consume("[", TK_RESERVED))
    {
      node = new_node(ND_ARRAY, node, expression(), old_token);
      node->variable.is_array_top = true;
      if (node->lhs->kind == ND_ARRAY)
        node->lhs->variable.is_array_top = false;
      expect("]", TK_RESERVED);
    }
    else if (consume(".", TK_RESERVED))
    {
      Token *token = expect_ident();
      node =
          new_node(ND_DOT, node, new_node(ND_FIELD, NULL, NULL, token), token);
    }
    else if (consume("->", TK_RESERVED))
    {
      Token *token = expect_ident();
      node = new_node(ND_ARROW, node, new_node(ND_FIELD, NULL, NULL, token),
                      token);
    }
    else if (consume("++", TK_RESERVED))
      node = new_node(ND_POSTINCREMENT, node, NULL, get_old_token());
    else if (consume("--", TK_RESERVED))
      node = new_node(ND_POSTDECREMENT, node, NULL, get_old_token());
    else
      return node;
  }
}

Node *primary_expression()
{
#if DEBUG
  Token *t = get_token();
  pr_debug2("Current token: '%.*s'", (int)t->len, t->str);
#endif
  if (consume("(", TK_RESERVED))
  {
    Node *node = expression();
    expect(")", TK_RESERVED);
    return node;
  }

  Token *token = consume_token_if_next_matches(TK_IDENT, '(');
  if (token)
  {
    Type *type = NULL;
    enum member_name result = is_enum_or_function_or_typedef_or_variables_name(
        token, NULL, &type, NULL);
    if (result == none_of_them)
    {
      Node *node = NULL;
      if (is_builtin_function(&node, token, false))
        return node;
      warn_at(token->str, token->len, "undefined function");
    }

    if (result == function_name || result == none_of_them)
    {
      if (type && !type->param_list)
        error_at(token->str, token->len, "invalid function call");
      Node *node = calloc(1, sizeof(Node));
      node->type = type;
      // Function call
      expect("(", TK_RESERVED);
      node->token = token;
      node->kind = ND_FUNCCALL;
      node->func.expr = vector_new();
      while (!consume(")", TK_RESERVED))
      {
        Node *child_node = assignment_expression();
        if (!child_node)
          error_exit("invalid node");
        vector_push(node->func.expr, child_node);
        if (!consume(",", TK_RESERVED))
        {
          expect(")", TK_RESERVED);
          break;
        }
      }
      return node;
    }
  }

  // Variable
  token = consume_ident();
  if (token)
  {
    size_t enum_number;
    Var *var = NULL;
    switch (is_enum_or_function_or_typedef_or_variables_name(
        token, &enum_number, NULL, &var))
    {  // Exclude enum cases
      case enum_member_name: return new_node_num(enum_number);
      case function_name: unreachable(); break;
      case variables_name:
      {  // It is known to be a variable
        Node *node = calloc(1, sizeof(Node));
        node->token = token;
        node->kind = ND_VAR;
        node->variable.is_new_var = false;
        node->variable.var = var;
        return node;
      }
      default:
      {
        if (token->len == 8 && !strncmp(token->str, "__func__", 8))
        {  // Support for __func__
          Node *node = calloc(1, sizeof(Node));
          node->token = calloc(1, sizeof(Token));
          node->token->kind = TK_STRING;
          node->token->str = program_name;
          node->token->len = program_name_len;
          node->kind = ND_STRING;
          return node;
        }
        error_at(token->str, token->len, "variables %.*s is not defined",
                 (int)token->len, token->str);
      }
    }
  }

  Token *string = consume_string();
  if (string)
  {
    Node *node = calloc(1, sizeof(Node));
    node->kind = ND_STRING;
    node->token = string;
    return node;
  }

  Token *char_token = consume_char();
  if (char_token)
  {
    size_t num_val;
    if (char_token->len == 2)
    {
      switch (char_token->str[1])
      {
        case 'n': num_val = '\n'; break;
        case 't': num_val = '\t'; break;
        case '\\': num_val = '\\'; break;
        case '\'': num_val = '\''; break;
        case '"': num_val = '"'; break;
        case '0': num_val = '\0'; break;
        case 'e': num_val = '\e'; break;
        default:
          error_at(char_token->str, char_token->len,
                   "unknown control character found");
          num_val = 0;
          break;
      }
    }
    else
      num_val = char_token->str[0];
    return new_node_num(num_val);
  }
  long long num;
  if (consume_number(&num))
    return new_node_num(num);
  return NULL;
}
