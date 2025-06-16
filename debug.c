#include "include/debug.h"

#include <assert.h>
#include <stdio.h>

#include "include/analyzer.h"
#include "include/conditional_inclusion.h"
#include "include/define.h"
#include "include/error.h"
#include "include/parser.h"
#include "include/tokenizer.h"
#include "include/type.h"
#include "include/variables.h"

extern GTLabel *head_label;
extern Vector *output_list;

// gdb で呼び出す用
void print_token_str(Vector *vec)
{
  pr_debug("start\n");
  for (size_t i = 1; i <= vector_size(vec); i++)
  {
    Token *token = vector_peek_at(vec, i);
    printf("%.*s", (int)token->len, token->str);
  }
  printf("\n");
  pr_debug("end");
}

static char *CPPTKlist[] = {CPPTK_list};

void print_polish_notation()
{
  pr_debug("polish notation:");
  printf("vector size: %lu\n", vector_size(output_list));
  for (size_t i = 1; i <= vector_size(output_list); i++)
  {
    conditional_inclusion_token *token = vector_peek_at(output_list, i);
    printf("%s", CPPTKlist[token->type]);
    if (token->type == CPPTK_Integer)
      printf(": %lld", token->num);
    printf("\n");
  }
  pr_debug("end polish notation");
}

void print_tokenize_result(Token *token)
{
  pr_debug("tokenize result:");
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

void _print_parse_result(Node *node, int nest)
{
  if (!node)
    return;
  make_space(nest);
  switch (node->kind)
  {
    case ND_NUM:
    {
      int type_name = __INT_MAX__;
      int reference_counter = -1;
      Type *pointer = node->type;
      while (pointer)
      {
        reference_counter++;
        type_name = pointer->type;
        pointer = pointer->ptr_to;
      }
      assert(reference_counter >= 0);
      fprintf(stdout, "NodeKind: %s value: %ld type: %.*s%d\n",
              nodekindlist[node->kind], node->val, reference_counter,
              "****************************************************************"
              "*************************************************",
              type_name);
      break;
    }
    case ND_VAR:
    {
      int type_name = __INT_MAX__;
      int reference_counter = -1;
      Type *pointer = node->type;
      while (pointer)
      {
        reference_counter++;
        type_name = pointer->type;
        pointer = pointer->ptr_to;
      }
      fprintf(stdout,
              "NodeKind: %s type: %.*s%d offset: %lu is_new: %s is_local: %s\n",
              nodekindlist[node->kind], reference_counter,
              "****************************************************************"
              "*************************************************",
              type_name, node->var->offset, node->is_new ? "true" : "false",
              node->var->is_local ? "true" : "false");
      break;
    }
    case ND_IF:
    case ND_ELIF:
    case ND_WHILE:
    case ND_DO:
    {
      fprintf(stdout, "NodeKind: %s labelname: %s\n", nodekindlist[node->kind],
              node->name->name);
      make_space(nest);
      fprintf(stdout, "|   [condition]\n");
      _print_parse_result(node->condition, nest + 1);
      make_space(nest);
      fprintf(stdout, "|   [true_code]\n");
      _print_parse_result(node->true_code, nest + 1);
      if (node->false_code)
      {
        make_space(nest);
        fprintf(stdout, "|    [false_code]\n");
        _print_parse_result(node->false_code, nest + 1);
      }
      break;
    }
    case ND_FOR:
    {
      fprintf(stdout, "NodeKind: %s\n", nodekindlist[node->kind]);
      if (node->init)
      {
        make_space(nest);
        fprintf(stdout, "|   [init]\n");
        _print_parse_result(node->init, nest + 1);
      }
      if (node->condition)
      {
        make_space(nest);
        fprintf(stdout, "|   [condition]\n");
        _print_parse_result(node->condition, nest + 1);
      }
      if (node->update)
      {
        make_space(nest);
        fprintf(stdout, "|   [update]\n");
        _print_parse_result(node->update, nest + 1);
      }
      if (node->true_code)
      {
        make_space(nest);
        fprintf(stdout, "|   [code]\n");
        _print_parse_result(node->true_code, nest + 1);
      }
      break;
    }
    case ND_BLOCK:
    {
      fprintf(stdout, "NodeKind: %s\n", nodekindlist[node->kind]);
      make_space(nest);
      fprintf(stdout, "|   [node]\n");
      for (NDBlock *pointer = node->stmt; pointer; pointer = pointer->next)
      {
        _print_parse_result(pointer->node, nest + 1);
      }
      break;
    }
    case ND_FUNCCALL:
    {
      fprintf(stdout, "NodeKind: %s\n", nodekindlist[node->kind]);
      make_space(nest);
      fprintf(stdout, "|   [arguments]\n");
      for (NDBlock *pointer = node->expr; pointer; pointer = pointer->next)
      {
        _print_parse_result(pointer->node, nest + 1);
      }
      break;
    }
    case ND_FUNCDEF:
    {
      fprintf(stdout, "NodeKind: %s\n", nodekindlist[node->kind]);
      make_space(nest);
      fprintf(stdout, "|   [arguments]\n");
      for (NDBlock *pointer = node->expr; pointer; pointer = pointer->next)
      {
        _print_parse_result(pointer->node, nest + 1);
      }
      fprintf(stdout, "|   [body]\n");
      for (NDBlock *pointer = node->stmt; pointer; pointer = pointer->next)
      {
        _print_parse_result(pointer->node, nest + 1);
      }
      break;
    }
    case ND_STRING:
    {
      fprintf(stdout, "NodeKind: %s name: %.*s\n", nodekindlist[node->kind],
              (int)node->token->len, node->token->str);
      break;
    }
    case ND_GOTO:
    {
      fprintf(stdout, "NodeKind: %s label_name: %s\n", nodekindlist[node->kind],
              node->label_name);
      make_space(nest);
      fprintf(stdout, "|   [statement]\n");
      _print_parse_result(node->statement_child, nest + 1);
      break;
    }
    case ND_LABEL:
    {
      fprintf(stdout, "NodeKind: %s label_name: %s\n", nodekindlist[node->kind],
              node->label_name);
      make_space(nest);
      fprintf(stdout, "|   [statement]\n");
      _print_parse_result(node->statement_child, nest + 1);
      break;
    }
    case ND_CASE:
    {
      fprintf(stdout, "NodeKind: %s type: %s expression(integer): %ld\n",
              nodekindlist[node->kind], node->is_case ? "case" : "default",
              node->constant_expression);
      make_space(nest);
      fprintf(stdout, "|   [statement]\n");
      _print_parse_result(node->statement_child, nest + 1);
      break;
    }
    case ND_SWITCH:
    {
      fprintf(stdout, "NodeKind: %s\n", nodekindlist[node->kind]);
      make_space(nest);
      fprintf(stdout, "|   [expression]\n");
      _print_parse_result(node->condition, nest + 1);
      make_space(nest);
      fprintf(stdout, "|   [code]\n");
      _print_parse_result(node->true_code, nest + 1);
      break;
    }
    case ND_TERNARY:
    {
      fprintf(stdout, "NodeKind: %s\n", nodekindlist[node->kind]);
      make_space(nest);
      fprintf(stdout, "|   [rhs]\n");
      _print_parse_result(node->rhs, nest + 1);
      make_space(nest);
      fprintf(stdout, "|   [chs]\n");
      _print_parse_result(node->chs, nest + 1);
      make_space(nest);
      fprintf(stdout, "|   [lhs]\n");
      _print_parse_result(node->lhs, nest + 1);
    }
    break;
    default:
    {
      fprintf(stdout, "NodeKind: %s\n", nodekindlist[node->kind]);
      if (node->lhs)
      {
        make_space(nest);
        fprintf(stdout, "|   [lhs]\n");
        _print_parse_result(node->lhs, nest + 1);
      }
      if (node->rhs)
      {
        make_space(nest);
        fprintf(stdout, "|   [rhs]\n");
        _print_parse_result(node->rhs, nest + 1);
      }
      break;
    }
  }
}

void print_parse_result(FuncBlock *node)
{
  pr_debug("parse result:");
  int i = 0;
  for (FuncBlock *pointer = node; pointer; pointer = pointer->next)
  {
    fprintf(stdout, "node[%d]\n", i++);
    _print_parse_result(pointer->node, 0);
  }
}

void print_definition()
{
  pr_debug("definition:");
  fprintf(stdout, "object_like_macro:\n");
  for (size_t i = 1; i <= vector_size(object_like_macro_list); i++)
  {
    object_like_macro_storage *tmp = vector_peek_at(object_like_macro_list, i);
    fprintf(stdout, "%.*s: ", (int)tmp->identifier->len, tmp->identifier->str);
    for (size_t j = 1; j <= vector_size(tmp->token_string); j++)
    {
      Token *token = vector_peek_at(tmp->token_string, j);
      fprintf(stdout, "%.*s ", (int)token->len, token->str);
    }
    fprintf(stdout, "\n");
  }
  fprintf(stdout, "fuction like macro:\n");
  for (size_t i = 1; i <= vector_size(function_like_macro_list); i++)
  {
    function_like_macro_storage *tmp =
        vector_peek_at(function_like_macro_list, i);
    fprintf(stdout, "%.*s(", (int)tmp->identifier->len, tmp->identifier->str);
    for (size_t j = 1; j <= vector_size(tmp->arguments); j++)
    {
      Token *token = vector_peek_at(tmp->arguments, j);
      fprintf(stdout, "%.*s,", (int)token->len, token->str);
    }
    fprintf(stdout, "):\n");
    for (size_t j = 1; j <= vector_size(tmp->token_string); j++)
    {
      Token *token = vector_peek_at(tmp->token_string, j);
      fprintf(stdout, "%.*s ", (int)token->len, token->str);
    }
    fprintf(stdout, "\n");
  }
}