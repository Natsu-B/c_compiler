#include "include/debug.h"

#ifdef SELF_HOST
#include "test/compiler_header.h"
#else
#include <assert.h>
#include <stdio.h>
#endif

#include "include/analyzer.h"
#include "include/conditional_inclusion.h"
#include "include/define.h"
#include "include/error.h"
#include "include/parser.h"
#include "include/tokenizer.h"
#include "include/type.h"

extern GTLabel *head_label;
extern Vector *output_list;

// for calling with gdb
void print_token_str(Vector *vec)
{
  pr_debug("Start\n");  // No Japanese here, just a comment
  for (size_t i = 1; i <= vector_size(vec); i++)
  {
    Token *token = vector_peek_at(vec, i);
    printf("%.*s", (int)token->len, token->str);
  }
  pr_debug("\nEnd");  // No Japanese here, just a comment
}

static char *CPPTKlist[CPPTK_Reserved] = {CPPTK_list};

void print_polish_notation()
{
  pr_debug("Polish notation:");
  printf("vector size: %lu\n", vector_size(output_list));
  for (size_t i = 1; i <= vector_size(output_list); i++)
  {
    conditional_inclusion_token *token = vector_peek_at(output_list, i);
    printf("%s", CPPTKlist[token->type]);
    if (token->type == CPPTK_Integer)
      printf(": %lld", token->num);
    printf("\n");
  }
  pr_debug("End polish notation");
}

void print_tokenize_result(Token *token)
{
  pr_debug("Tokenize result:");
  for (;;)
  {
    if (token->kind == TK_EOF)
      break;
    fprintf(stdout, "%.*s\t: %s\n", (int)token->len, token->str,
            tokenkindlist[token->kind]);
    token = token->next;
  }
}

void make_space(int nest)
{
  for (int i = 0; i < nest; i++)
    fprintf(stdout, "|   ");
}

void _print_parse_result(Node *node, int nest);

void print_type(Type *type)
{
  if (!type)
  {
    fprintf(stdout, "(null type)");
    return;
  }

  switch (type->type)
  {
    case TYPE_INT:
      fprintf(stdout, "int(signed:%d)", type->is_signed);
      break;
    case TYPE_BOOL:
      fprintf(stdout, "bool");
      break;
    case TYPE_CHAR:
      fprintf(stdout, "char(signed:%d)", type->is_signed);
      break;
    case TYPE_LONG:
      fprintf(stdout, "long(signed:%d)", type->is_signed);
      break;
    case TYPE_LONGLONG:
      fprintf(stdout, "long long(signed:%d)", type->is_signed);
      break;
    case TYPE_SHORT:
      fprintf(stdout, "short(signed:%d)", type->is_signed);
      break;
    case TYPE_VOID:
      fprintf(stdout, "void");
      break;
    case TYPE_STR:
      fprintf(stdout, "char*");
      break;
    case TYPE_STRUCT:
    {
      tag_list* tag = vector_peek_at(get_enum_struct_list(), type->type_num);
      fprintf(stdout, "struct %.*s", (int)tag->name->len, tag->name->str);
      break;
    }
    case TYPE_ENUM:
    {
      tag_list* tag = vector_peek_at(get_enum_struct_list(), type->type_num);
      fprintf(stdout, "enum %.*s", (int)tag->name->len, tag->name->str);
      break;
    }
    case TYPE_PTR:
      print_type(type->ptr_to);
      fprintf(stdout, "*");
      break;
    case TYPE_ARRAY:
      print_type(type->ptr_to);
      fprintf(stdout, "[%zu]", type->size);
      break;
    case TYPE_FUNC:
      fprintf(stdout, "func(");
      for (size_t i = 2; i <= vector_size(type->param_list); i++)
      {
        print_type(vector_peek_at(type->param_list, i));
        if (i + 1 <= vector_size(type->param_list))
          fprintf(stdout, ",");
      }
      fprintf(stdout, ") -> ");
      print_type(vector_peek_at(type->param_list, 1));
      break;
    case TYPE_VARIABLES:
      fprintf(stdout, "...");
      break;
    case TYPE_NULL:
      fprintf(stdout, "NULL");
      break;
    default:
      unreachable();
  }
}

void _print_parse_result(Node *node, int nest)
{
  if (!node)
    return;

  make_space(nest);
  fprintf(stdout, "NodeKind: %s", nodekindlist[node->kind]);

  if (node->token) {
    fprintf(stdout, " (%.*s)", (int)node->token->len, node->token->str);
  }

  // Print specific fields based on node kind
  switch (node->kind) {
    case ND_NUM:
      fprintf(stdout, " | value: %lld", node->val);
      break;
    case ND_VAR:
      fprintf(stdout, " | offset: %zu | is_new: %d | is_local: %d", node->var->offset, node->is_new, node->var->is_local);
      break;
    case ND_IF:
    case ND_ELIF:
    case ND_WHILE:
    case ND_DO:
    case ND_FOR:
    case ND_SWITCH:
    case ND_TERNARY:
    case ND_LOGICAL_OR:
    case ND_LOGICAL_AND:
      if (node->name)
        fprintf(stdout, " | label: %s", node->name->name);
      break;
    case ND_GOTO:
    case ND_LABEL:
      fprintf(stdout, " | label: %s", node->label_name);
      break;
    case ND_CASE:
      fprintf(stdout, " | %s", node->is_case ? "case" : "default");
      if (node->is_case)
        fprintf(stdout, " %ld", node->constant_expression);
      break;
    case ND_FIELD:
      fprintf(stdout, " | offset: %lu", node->child_offset);
      break;
    default:
      break;
  }

  if (node->type) {
    fprintf(stdout, " | type: ");
    print_type(node->type);
  }

  fprintf(stdout, "\n");

  if (node->init) {
    make_space(nest);
    fprintf(stdout, "|-init:\n");
    _print_parse_result(node->init, nest + 1);
  }
  if (node->condition) {
    make_space(nest);
    fprintf(stdout, "|-cond:\n");
    _print_parse_result(node->condition, nest + 1);
  }
  if (node->lhs) {
    make_space(nest);
    fprintf(stdout, "|-lhs:\n");
    _print_parse_result(node->lhs, nest + 1);
  }
  if (node->chs) {
    make_space(nest);
    fprintf(stdout, "|-chs:\n");
    _print_parse_result(node->chs, nest + 1);
  }
  if (node->rhs) {
    make_space(nest);
    fprintf(stdout, "|-rhs:\n");
    _print_parse_result(node->rhs, nest + 1);
  }
  if (node->true_code) {
    make_space(nest);
    fprintf(stdout, "|-then:\n");
    _print_parse_result(node->true_code, nest + 1);
  }
  if (node->false_code) {
    make_space(nest);
    fprintf(stdout, "|-else:\n");
    _print_parse_result(node->false_code, nest + 1);
  }
  if (node->update) {
    make_space(nest);
    fprintf(stdout, "|-update:\n");
    _print_parse_result(node->update, nest + 1);
  }
  if (node->statement_child) {
    _print_parse_result(node->statement_child, nest + 1);
  }
  if (node->expr) {
    make_space(nest);
    fprintf(stdout, "|-exprs:\n");
    for (size_t i = 1; i <= vector_size(node->expr); i++) {
      _print_parse_result(vector_peek_at(node->expr, i), nest + 1);
    }
  }
  if (node->stmt) {
    make_space(nest);
    fprintf(stdout, "|-block:\n");
    for (NDBlock *p = node->stmt; p; p = p->next) {
      _print_parse_result(p->node, nest + 1);
    }
  }
}

void print_parse_result(FuncBlock *node)
{
  pr_debug("Parse result:");
  int i = 0;
  for (FuncBlock *pointer = node; pointer; pointer = pointer->next)
  {
    if (pointer->node && pointer->node->kind != ND_NOP)
    {
      fprintf(stdout, "--- node[%d] ---\n", i++);
      _print_parse_result(pointer->node, 0);
    }
  }
}

void print_definition()
{
  pr_debug("Definition:");
  fprintf(stdout, "Object-like macro:\n");
  for (size_t i = 1; i <= vector_size(object_like_macro_list); i++)
  {
    object_like_macro_storage *tmp = vector_peek_at(object_like_macro_list, i);
    fprintf(stdout, "  %.*s: ", (int)tmp->identifier->len, tmp->identifier->str);
    for (size_t j = 1; j <= vector_size(tmp->token_string); j++)
    {
      Token *token = vector_peek_at(tmp->token_string, j);
      fprintf(stdout, "%.*s ", (int)token->len, token->str);
    }
    fprintf(stdout, "\n");
  }
  fprintf(stdout, "Function-like macro:\n");
  for (size_t i = 1; i <= vector_size(function_like_macro_list); i++)
  {
    function_like_macro_storage *tmp =
        vector_peek_at(function_like_macro_list, i);
    fprintf(stdout, "  %.*s(", (int)tmp->identifier->len, tmp->identifier->str);
    for (size_t j = 1; j <= vector_size(tmp->arguments); j++)
    {
      Token *token = vector_peek_at(tmp->arguments, j);
      fprintf(stdout, "%.*s", (int)token->len, token->str);
      if (j < vector_size(tmp->arguments))
        fprintf(stdout, ", ");
    }
    fprintf(stdout, "): ");
    for (size_t j = 1; j <= vector_size(tmp->token_string); j++)
    {
      Token *token = vector_peek_at(tmp->token_string, j);
      fprintf(stdout, "%.*s ", (int)token->len, token->str);
    }
    fprintf(stdout, "\n");
  }
}
