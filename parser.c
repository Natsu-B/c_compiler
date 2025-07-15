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

#include "include/debug.h"
#include "include/error.h"
#include "include/eval_constant.h"
#include "include/tokenizer.h"
#include "include/type.h"
#include "include/variables.h"
#include "include/vector.h"

Type *abstract_declarator(Type *type);
Type *direct_abstract_declarator(Type *type);
Node *external_declaration();
Node *function_declaration();
Node *declaration(Type *type, bool is_external_declaration);
Node *init_declarator(Type *type);
Node *declarator_no_side_effect();
Node *declarator(Type *type);
Type *pointer(Type *type);
Node *type_name();
Node *initializer();
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

// 同じnestのGTLabelを保持しているvectorを保持するvector
// nestが深くなるとき新たなvectorがpushされ浅くなるときpopされる
Vector *label_list;
// 現在パースしている関数名と名前
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

// break continueで飛ぶ場所を探す関数
// 引数のtypeに1が来たときcontinueで2が来たときbreakとする
char *find_jmp_target(size_t type)
{
  // while, for文を探す ネストが深いほうから探していく
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

// goto文のラベルを.Lgoto_YYY_XXX (YYYにラベル名、XXXに関数名)とする
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
  node->val = val;
  return node;
}

NestedBlockVariables *new_nest()
{
  new_nest_type();
  new_nest_label();
  return new_nest_variables();
}

void exit_nest()
{
  exit_nest_type();
  exit_nest_label();
  exit_nest_variables();
}

Vector *parameter_type_list(Vector **type_list, Type *type)
{  // 関数の引数のnode リストを作成 ")"をconsumeして終了する
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
      Type *type = declaration_specifiers();
      if (!type)
        error_at(get_token()->str, get_token()->len, "invalid type");
      Node *parameter = declarator_no_side_effect(type);
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
  pr_debug("start parser...");
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
  Type *type = declaration_specifiers();
  if (!type)
    error_at(get_token()->str, get_token()->len, "failed to parse");
  return declaration(type, true);
}

Node *declaration(Type *type, bool is_external_declaration)
{
  Node *node = init_declarator(type);
  // funcdef
  if (is_external_declaration && consume("{", TK_RESERVED))
  {
    program_name = node->token->str;
    program_name_len = node->token->len;
    new_nest();
    for (size_t i = 1; i <= vector_size(node->expr); i++)
    {
      Node *param = vector_peek_at(node->expr, i);
      param->var = add_variables(param->token, param->type);
      param->is_new = true;
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
    node->stmt = head.next;
    exit_nest();
    return node;
  }
  else if (node && node->kind == ND_FUNCDEF)
    memset(node, 0, sizeof(Node));
  expect(";", TK_RESERVED);
  return node;
}

Node *init_declarator(Type *type)
{
  Node *node = declarator(type);
  if (!node)
    return NULL;
  Token *old = get_token();
  if (consume("=", TK_RESERVED))
    node = new_node(ND_ASSIGN, node, assignment_expression(), old);
  return node;
}

Node *declarator_internal(Type **type, Token *token)
{
  if (!token)
  {
    if (consume("(", TK_RESERVED))
    {
      Node *result = declarator(*type);
      expect(")", TK_RESERVED);
      return result;
    }
    return NULL;
  }
  if (consume("[", TK_RESERVED))
  {
    Type *new = alloc_type(TYPE_ARRAY);
    new->ptr_to = *type;
    new->size = eval_constant_expression();
    *type = new;
    expect("]", TK_RESERVED);
  }
  else if (consume("(", TK_RESERVED))
  {
    Node *node = calloc(1, sizeof(Node));
    node->kind = ND_FUNCDEF;
    node->func_name = token->str;
    node->func_len = token->len;
    node->type = *type;
    node->token = token;
    Type *new = alloc_type(TYPE_FUNC);
    new->param_list = vector_new();
    node->expr = parameter_type_list(&new->param_list, *type);
    if (!add_function_name(new->param_list, token))
      error_at(token->str, token->len, "invalid name");
    *type = new;
    return node;
  }
  Node *node = calloc(1, sizeof(Node));
  node->token = token;
  node->kind = ND_VAR;
  node->type = *type;
  return node;
}

Node *declarator_no_side_effect(Type *type)
{
  type = pointer(type);
  Token *token = consume_ident();
  return declarator_internal(&type, token);
}

Node *declarator(Type *type)
{
  type = pointer(type);
  Token *token = consume_ident();
  Node *node = declarator_internal(&type, token);
  if (node && type->type != TYPE_FUNC)
  {
    Var *var = add_variables(token, type);
    if (!var)
      return new_node(ND_NOP, NULL, NULL, NULL);
    node->var = var;
    if (type)
      node->is_new = true;
  }
  return node;
}

Type *pointer(Type *type)
{
  while (consume("*", TK_RESERVED))
  {
    Type *new = alloc_type(TYPE_PTR);
    new->ptr_to = type;
    type = new;
  }
  return type;
}

Node *type_name()
{
  Type *type = declaration_specifiers();
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

// direct_abstract_declaratorに当てはまらなくても許容する
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
    if (consume("[", TK_RESERVED))
    {
      Type *new = alloc_type(TYPE_ARRAY);
      new->ptr_to = type;
      if (peek("]", TK_RESERVED))
      {
        new->size = 0;
      }
      else
      {
        new->size = eval_constant_expression();
      }
      expect("]", TK_RESERVED);
      type = new;
      continue;
    }

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
        parameter_type_list(&func_type->param_list, tail);

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
        parameter_type_list(&new->param_list, type);
        type = new;
      }
    }
    break;
  }
  return type;
}

Node *initializer()
{
  return assignment_expression();
}

Node *block_item()
{
  Type *type = declaration_specifiers();
  if (type)
    return declaration(type, false);
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
      node->is_case = true;
      node->constant_expression = eval_constant_expression();
    }
    else
      node->is_case = false;
    expect(":", TK_RESERVED);
    node->statement_child = statement();
    return node;
  }
  Token *token_ident = consume_token_if_next_matches(TK_IDENT, ':');
  if (token_ident)
  {
    expect(":", TK_RESERVED);
    node = new_node(ND_LABEL, NULL, NULL, token_ident);
    node->label_name = mangle_goto_label(token_ident);
    node->statement_child = statement();
    return node;
  }
  return node;
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
    node->stmt = head.next;
    return node;
  }

  // selection-statement
  // if文の判定とelseがついてるかどうか
  if (consume("if", TK_IDENT))
  {
    Node *node = calloc(1, sizeof(Node));
    new_nest();
    node->name = generate_label_name(ND_IF);
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
  if (consume("switch", TK_IDENT))
  {
    Node *node = calloc(1, sizeof(Node));
    node->kind = ND_SWITCH;
    new_nest();
    expect("(", TK_RESERVED);
    node->name = generate_label_name(ND_SWITCH);
    node->condition = expression();
    expect(")", TK_RESERVED);
    node->true_code = statement();
    exit_nest();
    return node;
  }

  // iteration-statement
  // while文の判定
  if (consume("while", TK_IDENT))
  {
    Node *node = calloc(1, sizeof(Node));
    node->kind = ND_WHILE;
    new_nest();
    node->name = generate_label_name(ND_WHILE);
    expect("(", TK_RESERVED);
    node->condition = expression();
    expect(")", TK_RESERVED);
    node->true_code = statement();
    exit_nest();
    return node;
  }
  // do while文の判定
  if (consume("do", TK_IDENT))
  {
    new_nest();
    Node *node = calloc(1, sizeof(Node));
    node->name = generate_label_name(ND_DO);
    node->true_code = statement();
    node->kind = ND_DO;
    expect("while", TK_IDENT);
    expect("(", TK_RESERVED);
    node->condition = expression();
    expect(")", TK_RESERVED);
    expect(";", TK_RESERVED);
    exit_nest();
    return node;
  }
  // for文の判定
  if (consume("for", TK_IDENT))
  {
    Node *node = calloc(1, sizeof(Node));
    node->kind = ND_FOR;
    new_nest();
    node->name = generate_label_name(ND_FOR);
    expect("(", TK_RESERVED);
    // declarationかどうか
    Type *type = declaration_specifiers();
    if (type)
      node->init = declaration(type, false);
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

  Node *node = labeled_statement();
  if (node)
    return node;
  if (consume("return", TK_IDENT))
    node = new_node(ND_RETURN, expression(), NULL, get_old_token());
  else if (consume("continue", TK_IDENT))
  {
    node = new_node(ND_GOTO, NULL, NULL, get_old_token());
    node->label_name = find_jmp_target(1);
  }
  else if (consume("break", TK_IDENT))
  {
    node = new_node(ND_GOTO, NULL, NULL, get_old_token());
    node->label_name = find_jmp_target(2);
  }
  else if (consume("goto", TK_IDENT))
  {
    node = new_node(ND_GOTO, NULL, NULL, get_old_token());
    node->label_name = mangle_goto_label(expect_ident());
  }
  else
    node = new_node(ND_DISCARD_EXPR, expression(), NULL, get_token());
  expect(";", TK_RESERVED);
  return node;
}

Node *expression()
{
  pr_debug2("expression");
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
  pr_debug2("assignment-expression");
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
    node->name = generate_label_name(ND_TERNARY);
    node->chs = chs;
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
    node->name = generate_label_name(ND_LOGICAL_OR);
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
    node->name = generate_label_name(ND_LOGICAL_AND);
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
  pr_debug2("unary");
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
  pr_debug2("postfix");
  Node *node = primary_expression();
  for (;;)
  {
    Token *old_token = get_old_token();
    if (consume("[", TK_RESERVED))
    {
      node = new_node(ND_ARRAY, node, expression(), old_token);
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
  pr_debug2("primary");
  if (consume("(", TK_RESERVED))
  {
    Node *node = expression();
    expect(")", TK_RESERVED);
    return node;
  }

  Token *token = consume_token_if_next_matches(TK_IDENT, '(');
  if (token)
  {
    enum member_name result = is_enum_or_function_or_typedef_name(token, NULL);
    if (result == none_of_them)
      warn_at(token->str, token->len, "undefined function");
    if (result == function_name || result == none_of_them)
    {
      // 関数呼び出し
      expect("(", TK_RESERVED);
      Node *node = calloc(1, sizeof(Node));
      node->token = token;
      node->kind = ND_FUNCCALL;
      node->func_name = token->str;
      node->func_len = token->len;
      node->expr = vector_new();
      while (!consume(")", TK_RESERVED))
      {
        Node *child_node = assignment_expression();
        if (!child_node)
          error_exit("invalid node");
        vector_push(node->expr, child_node);
        if (!consume(",", TK_RESERVED))
        {
          expect(")", TK_RESERVED);
          break;
        }
      }
      return node;
    }
  }

  // 変数
  token = consume_ident();
  if (token)
  {
    size_t enum_number;
    switch (is_enum_or_function_or_typedef_name(token, &enum_number))
    {  // enumの場合を除外
      case enum_member_name: return new_node_num(enum_number);
      case function_name: unreachable(); break;
      case none_of_them:
      {  // 変数であるとわかる
        Node *node = calloc(1, sizeof(Node));

        if (token->len == 8 && !strncmp(token->str, "__func__", 8))
        {  // __func__ の場合をサポート
          node->token = calloc(1, sizeof(Token));
          node->token->kind = TK_STRING;
          char *tmp = malloc(program_name_len + 2);
          tmp[0] = tmp[program_name_len + 1] = '"';
          strncpy(tmp + 1, program_name, program_name_len);
          node->token->str = tmp;
          node->token->len = program_name_len + 2;
          node->kind = ND_STRING;
        }
        else
        {
          node->token = token;
          node->kind = ND_VAR;
          node->is_new = false;
          node->var = add_variables(token, NULL);
        }
        return node;
      }
      default: unreachable();
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

  return new_node_num(expect_number());
}
