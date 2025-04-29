// ------------------------------------------------------------------------------------
// parser
// ------------------------------------------------------------------------------------

#include "include/parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "include/debug.h"
#include "include/error.h"
#include "include/tokenizer.h"
#include "include/type.h"
#include "include/variables.h"

const char *nodekindlist[] = {NodeKindTable};

GTLabel *head_label;
// 現在パースしている関数名と名前
char *program_name;
int program_name_len;

GTLabel *generate_label_name()
{
  size_t namesize = program_name_len + 6;
  GTLabel *next = calloc(1, sizeof(GTLabel));
  next->len = namesize;
  next->next = head_label;
  char *mangle_name = malloc(namesize);
  int i = 0;
  for (;;)
  {
    snprintf(mangle_name, namesize, "_%d_%.*s", i++, program_name_len,
             program_name);
    GTLabel *var = head_label;
    for (;;)
    {
      // 全部探索し終えたら
      if (!var)
      {
        next->name = mangle_name;
        head_label = next;
        return next;
      }
      if (namesize == var->len && !strncmp(mangle_name, var->name, namesize))
      {
        break;
      }
      var = var->next;
    }
  }
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

Node *new_node_num(int val)
{
  Node *node = calloc(1, sizeof(Node));
  node->kind = ND_NUM;
  node->val = val;
  Type *type = calloc(1, sizeof(Type));
  type->type = TYPE_INT;
  node->type = type;
  return node;
}

NestedBlockVariables *new_nest()
{
  new_nest_type();
  return new_nest_variables();
}

void exit_nest()
{
  exit_nest_type();
  exit_nest_variables();
}

Node *external_declaration();
Node *function_declaration();
Node *declaration(Type *type);
Node *init_declarator(Type *type);
Node *declarator(Type *type);
Node *initializer();
Node *block_item();
Node *statement();
Node *expression();
Node *assignment_expression();
Node *conditional_expression();
Node *equality_expression();
Node *relational_expression();
Node *shift_expression();
Node *additive_expression();
Node *multiplicative_expression();
Node *cast_expression();
Node *unary_expression();
Node *postfix_expression();
Node *primary_expression();

FuncBlock *parser()
{
  pr_debug("start parser...");
  FuncBlock head;
  head.next = NULL;
  FuncBlock *pointer = &head;
  init_variables();
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
  pr_debug("complite parse");
  return head.next;
}

Node *external_declaration()
{
  pr_debug2("program");
  Token *token_before = get_token();
  Type *type = declaration_specifiers();
  if (!type)
    error_at(token_before->str, token_before->len, "型が指定されていません");

  // 関数宣言かどうか function-definition
  Token *token = consume_token_if_next_matches(TK_IDENT, '(');
  if (token)
  {
    expect("(", TK_RESERVED);
    Node *node = calloc(1, sizeof(Node));
    node->kind = ND_FUNCDEF;
    node->func_name = token->str;
    node->func_len = token->len;
    node->var_list = new_nest();
    node->type = type;
    program_name = token->str;
    program_name_len = token->len;
    NDBlock head;
    head.next = NULL;
    NDBlock *pointer = &head;
    bool is_comma = true;
    while (!consume(")", TK_RESERVED))
    {
      if (!is_comma)
        error_at(get_token()->str, get_token()->len, "invalid parameter");
      is_comma = false;
      NDBlock *next = calloc(1, sizeof(NDBlock));
      pointer->next = next;
      next->node = declarator(declaration_specifiers());
      pointer = next;
      is_comma = consume(",", TK_RESERVED);
    }

    node->expr = head.next;
    head.next = NULL;
    pointer = &head;
    expect("{", TK_RESERVED);
    while (!consume("}", TK_RESERVED))
    {
      NDBlock *next = calloc(1, sizeof(NDBlock));
      pointer->next = next;
      next->node = block_item();
      pointer = next;
    }
    node->stmt = head.next;
    exit_nest();
    return node;
  }
  // グローバル変数宣言
  Node *node = declaration(type);
  return node;
}

Node *declaration(Type *type)
{
  Node *node = init_declarator(type);
  expect(";", TK_RESERVED);
  return node;
}

Node *init_declarator(Type *type)
{
  Node *node = declarator(type);
  if (consume("=", TK_RESERVED))
    node = new_node(ND_ASSIGN, node, initializer(), get_old_token());
  return node;
}

Node *declarator(Type *type)
{
  Token *token = consume_ident();
  if (consume("[", TK_RESERVED))
  {
    Type *new = alloc_type(TYPE_ARRAY);
    new->ptr_to = type;
    new->size = expect_number();
    type = new;
    expect("]", TK_RESERVED);
  }
  Var *var = add_variables(token, type);
  Node *node = NULL;
  if (var)
  {
    node = calloc(1, sizeof(Node));
    node->token = token;
    node->kind = ND_VAR;
    node->is_new = true;
    node->var = var;
  }
  return node;
}

Node *initializer()
{
  return assignment_expression();
}

Node *block_item()
{
  Type *type = declaration_specifiers();
  if (type)
    return declaration(type);
  return statement();
}

Node *statement()
{
  pr_debug2("statement");
  // ブロックの判定 compound-statement
  if (consume("{", TK_RESERVED))
  {
    Node *node = calloc(1, sizeof(Node));
    NDBlock head;
    head.next = NULL;
    NDBlock *pointer = &head;
    node->kind = ND_BLOCK;
    node->var_list = new_nest();
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
    node->stmt = head.next;
    return node;
  }

  // selection-statement
  // if文の判定とelseがついてるかどうか
  if (consume("if", TK_IDENT))
  {
    Node *node = calloc(1, sizeof(Node));
    node->name = generate_label_name();
    node->nest_var = new_nest();
    expect("(", TK_RESERVED);
    node->condition = expression();
    expect(")", TK_RESERVED);
    node->true_code = statement();
    if (consume("else", TK_IDENT))
    {
      node->kind = ND_ELIF;
      node->false_code = statement();
    }
    else
    {
      node->kind = ND_IF;
    }
    exit_nest();
    return node;
  }

  // iteration-statement
  // while文の判定
  if (consume("while", TK_IDENT))
  {
    Node *node = calloc(1, sizeof(Node));
    node->kind = ND_WHILE;
    node->name = generate_label_name();
    node->nest_var = new_nest();
    expect("(", TK_RESERVED);
    node->condition = expression();
    expect(")", TK_RESERVED);
    node->true_code = statement();
    exit_nest();
    return node;
  }
  // for文の判定
  if (consume("for", TK_IDENT))
  {
    Node *node = calloc(1, sizeof(Node));
    node->kind = ND_FOR;
    node->name = generate_label_name();
    node->nest_var = new_nest();
    expect("(", TK_RESERVED);
    // declarationかどうか
    Type *type = declaration_specifiers();
    if (type)
      node->init = declaration(type);
    else if (!consume(";", TK_RESERVED))
    {
      Node *new = calloc(1, sizeof(Node));
      node->init = new;
      new->kind = ND_DISCARD_EXPR;
      new->lhs = expression();
      expect(";", TK_RESERVED);
    }
    if (!consume(";", TK_RESERVED))
    {
      node->condition = expression();
      expect(";", TK_RESERVED);
    }
    if (!consume(")", TK_RESERVED))
    {
      Node *new = calloc(1, sizeof(Node));
      node->update = new;
      new->kind = ND_DISCARD_EXPR;
      new->lhs = expression();
      expect(")", TK_RESERVED);
    }
    node->true_code = statement();
    exit_nest();
    return node;
  }

  Node *node;
  if (consume("return", TK_IDENT))
    node = new_node(ND_RETURN, expression(), NULL, get_old_token());
  else
    node = new_node(ND_DISCARD_EXPR, expression(), NULL, get_token());
  expect(";", TK_RESERVED);
  return node;
}

Node *expression()
{
  pr_debug2("expression");
  return assignment_expression();
}

Node *assignment_expression()
{
  pr_debug2("assignment-expression");
  Node *node = conditional_expression();

  if (consume("=", TK_RESERVED))
  {
    node = new_node(ND_ASSIGN, node, assignment_expression(), get_old_token());
  }
  return node;
}

Node *conditional_expression()
{
  // logical-OR-expression
  // logical-AND-expression
  // inclusive-OR-expression
  // exclusive-OR-expression
  // AND-expression
  // equality-expression
  return equality_expression();
}

Node *equality_expression()
{
  pr_debug2("equality");
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
  pr_debug2("relational");
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
  return additive_expression();
}

Node *additive_expression()
{
  pr_debug2("add");
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
  pr_debug2("mul");
  Node *node = cast_expression();

  for (;;)
  {
    if (consume("*", TK_RESERVED))
      node = new_node(ND_MUL, node, cast_expression(), get_old_token());
    else if (consume("/", TK_RESERVED))
      node = new_node(ND_DIV, node, cast_expression(), get_old_token());
    else
      return node;
  }
}

Node *cast_expression()
{
  return unary_expression();
}

Node *unary_expression()
{
  pr_debug2("unary");
  if (consume("sizeof", TK_IDENT))
    return new_node(ND_SIZEOF, cast_expression(), NULL, get_old_token());
  if (consume("+", TK_RESERVED))
    return cast_expression();
  if (consume("-", TK_RESERVED))
    return new_node(ND_SUB, new_node_num(0), cast_expression(),
                    get_old_token());
  if (consume("*", TK_RESERVED))
    return new_node(ND_DEREF, cast_expression(), NULL, get_old_token());
  if (consume("&", TK_RESERVED))
    return new_node(ND_ADDR, cast_expression(), NULL, get_old_token());
  return postfix_expression();
}

Node *postfix_expression()
{
  pr_debug2("postfix");
  Node *node = primary_expression();
  for (;;)
  {
    Token *old_token = get_old_token();
    if (consume("[", TK_RESERVED))
    {
      node = new_node(ND_ARRAY, node, new_node_num(expect_number()), old_token);
      expect("]", TK_RESERVED);
    }
    return node;
  }
}

Node *primary_expression()
{
  pr_debug2("primary");
  if (consume("(", TK_RESERVED))
  {
    Node *node = expression();
    expect(")", TK_RESERVED);
    return node;
  }

  Token *token = consume_token_if_next_matches(TK_IDENT, '(');
  // 関数
  if (token)
  {
    expect("(", TK_RESERVED);
    Node *node = calloc(1, sizeof(Node));
    node->token = token;
    NDBlock head;
    head.next = NULL;
    NDBlock *pointer = &head;
    node->kind = ND_FUNCCALL;
    node->func_name = token->str;
    node->func_len = token->len;
    while (!consume(")", TK_RESERVED))
    {
      NDBlock *next = calloc(1, sizeof(NDBlock));
      pointer->next = next;
      next->node = expression();
      pointer = next;
      if (!consume(",", TK_RESERVED))
      {
        expect(")", TK_RESERVED);
        break;
      }
    }
    node->expr = head.next;
    return node;
  }

  // 変数
  token = consume_ident();
  if (token)
  {
    Node *node = calloc(1, sizeof(Node));
    node->token = token;
    node->kind = ND_VAR;
    node->is_new = false;
    node->var = add_variables(token, NULL);
    return node;
  }

  if (consume_string())
  {
    Node *node = calloc(1, sizeof(Node));
    node->kind = ND_STRING;
    Token *token = get_old_token();
    node->token = token;
    node->type = alloc_type(TYPE_STR);
    return node;
  }

  return new_node_num(expect_number());
}
