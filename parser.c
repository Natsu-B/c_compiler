// ------------------------------------------------------------------------------------
// parser
// ------------------------------------------------------------------------------------

#include "include/parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "include/debug.h"
#include "include/error.h"
#include "include/eval_constant.h"
#include "include/tokenizer.h"
#include "include/type.h"
#include "include/variables.h"
#include "include/vector.h"

const char *nodekindlist[] = {NodeKindTable};

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
  vector_push(label_list, NULL);
}

void exit_nest_label()
{
  vector_pop(label_list);
}

GTLabel *generate_label_name(NodeKind kind)
{
  static char *old_label_program_name;
  static int old_label_program_name_len;
  static size_t label_name_counter;

  if (old_label_program_name_len != program_name_len ||
      strncmp(old_label_program_name, program_name, program_name_len))
    label_name_counter = 0;  // その関数がlabelを初めてつける関数の場合初期化
  old_label_program_name = program_name;
  old_label_program_name_len = program_name_len;
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
  if (vector_pop(label_list))
    unreachable();
  vector_push(label_list, next);
  return next;
}

// break continueで飛ぶ場所を探す関数
// 引数のtypeに1が来たときcontinueで2が来たときbreakとする
char *find_jmp_target(size_t type)
{
  // while, for文を探す ネストが深いほうから探していく
  for (size_t i = vector_size(label_list); i >= 1; i--)
  {
    GTLabel *labeled_loop = vector_peek_at(label_list, i);
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
  new_nest_label();
  return new_nest_variables();
}

void exit_nest()
{
  exit_nest_type();
  exit_nest_label();
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
Node *constant_expression();
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
    new_nest();
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

Node *constant_expression()
{
  return conditional_expression();
}

Node *conditional_expression()
{
  // logical-OR-expression
  // logical-AND-expression
  // inclusive-OR-expression
  // exclusive-OR-expression
  // AND-expression
  // equality-expression
  Node *node = equality_expression();
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
  if (consume("++", TK_RESERVED))
    return new_node(ND_PREINCREMENT, unary_expression(), NULL, get_old_token());
  if (consume("--", TK_RESERVED))
    return new_node(ND_PREDECREMENT, unary_expression(), NULL, get_old_token());
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
    size_t enum_number;
    switch (is_enum_or_function_name(token, &enum_number))
    {
      case enum_member_name: return new_node_num(enum_number);
      case function_name: unimplemented(); break;
      case none_of_them:
      {
        Node *node = calloc(1, sizeof(Node));
        node->token = token;
        node->kind = ND_VAR;
        node->is_new = false;
        node->var = add_variables(token, NULL);
        return node;
      }
      default: unreachable();
    }
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
