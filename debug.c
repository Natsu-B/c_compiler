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
    printf("%.*s\t: %s\n", (int)token->len, token->str,
           tokenkindlist[token->kind]);
    token = token->next;
  }
}

void make_space(int nest)
{
  for (int i = 0; i < nest; i++)
    printf("|   ");
}

void _print_parse_result(Node *node, int nest);

void print_type(Type *type)
{
  if (!type)
  {
    printf("(null type)");
    return;
  }

  switch (type->type)
  {
    case TYPE_INT: printf("int(signed:%d)", type->is_signed); break;
    case TYPE_BOOL: printf("bool"); break;
    case TYPE_CHAR: printf("char(signed:%d)", type->is_signed); break;
    case TYPE_LONG: printf("long(signed:%d)", type->is_signed); break;
    case TYPE_LONGLONG: printf("long long(signed:%d)", type->is_signed); break;
    case TYPE_SHORT: printf("short(signed:%d)", type->is_signed); break;
    case TYPE_VOID: printf("void"); break;
    case TYPE_STR: printf("char*"); break;
    case TYPE_STRUCT:
    {
      tag_list *tag = vector_peek_at(get_enum_struct_list(), type->type_num);
      if (tag->name)
        printf("struct %.*s", (int)tag->name->len, tag->name->str);
      else
        printf("struct (unknown)");
      break;
    }
    case TYPE_ENUM:
    {
      tag_list *tag = vector_peek_at(get_enum_struct_list(), type->type_num);
      if (tag->name)
        printf("enum %.*s", (int)tag->name->len, tag->name->str);
      else
        printf("enum (unknown)");
      break;
    }
    case TYPE_PTR:
      print_type(type->ptr_to);
      printf("*");
      break;
    case TYPE_ARRAY:
      printf("[%zu]", type->size);
      print_type(type->ptr_to);
      break;
    case TYPE_FUNC:
      printf("func(");
      for (size_t i = 2; i <= vector_size(type->param_list); i++)
      {
        print_type(vector_peek_at(type->param_list, i));
        if (i + 1 <= vector_size(type->param_list))
          printf(",");
      }
      printf(") -> ");
      print_type(vector_peek_at(type->param_list, 1));
      break;
    case TYPE_VARIABLES: printf("..."); break;
    case TYPE_NULL: printf("NULL"); break;
    default: unreachable();
  }
}

void _print_parse_result(Node *node, int nest)
{
  if (!node)
    return;

  make_space(nest);
  printf("NodeKind: %s", nodekindlist[node->kind]);

  if (node->token)
  {
    printf(" (%.*s)", (int)node->token->len, node->token->str);
  }

  if (node->type)
  {
    printf(" | type: ");
    print_type(node->type);
  }

  switch (node->kind)
  {
    case ND_NUM: printf("| value: %lld", node->num_val); break;
    case ND_VAR:
      printf("| offset: %zu | is_new: %d | is_local: %d",
             node->variable.var->offset, node->variable.is_new_var,
             node->variable.var->is_local);
      break;
    case ND_FUNCDEF:
      printf("| storage class: %u", node->func.storage_class_specifier);
      break;
    case ND_IF:
    case ND_ELIF:
    case ND_FOR:
    case ND_WHILE:
    case ND_DO:
    case ND_TERNARY:
    case ND_LOGICAL_OR:
    case ND_LOGICAL_AND:
    case ND_SWITCH:
      if (node->control.label)
        printf(" | label: %s", node->control.label->name);
      break;
    case ND_GOTO:
    case ND_LABEL: printf("| label: %s\n", node->jump.label_name); break;
    case ND_CASE:
      printf("| %s", node->jump.is_case ? "case" : "default\n");
      break;
    case ND_FIELD: printf("| offset: %lu\n", node->child_offset); break;
    default: break;
  }

  printf("\n");
  if (node->lhs)
  {
    make_space(nest);
    printf("|-lhs:\n");
    _print_parse_result(node->lhs, nest + 1);
  }
  if (node->rhs)
  {
    make_space(nest);
    printf("|-rhs:\n");
    _print_parse_result(node->rhs, nest + 1);
  }

  // Print specific fields and recurse based on node kind
  switch (node->kind)
  {
    case ND_FUNCDEF:
    case ND_FUNCCALL:
    case ND_BUILTINFUNC:
      if (node->func.expr)
      {
        make_space(nest);
        printf("|-exprs:\n");
        for (size_t i = 1; i <= vector_size(node->func.expr); i++)
        {
          _print_parse_result(vector_peek_at(node->func.expr, i), nest + 1);
        }
      }
      if (node->func.stmt)
      {  // For ND_FUNCDEF and ND_BLOCK
        make_space(nest);
        printf("|-block:\n");
        for (NDBlock *p = node->func.stmt; p; p = p->next)
        {
          _print_parse_result(p->node, nest + 1);
        }
      }
      break;
    case ND_TYPE_NAME:
      // No child nodes to print directly from Node structure
      break;
    case ND_IF:
    case ND_ELIF:
    case ND_FOR:
    case ND_WHILE:
    case ND_DO:
    case ND_TERNARY:
    case ND_LOGICAL_OR:
    case ND_LOGICAL_AND:
    case ND_SWITCH:
      if (node->control.init)
      {
        make_space(nest);
        printf("|-init:\n");
        _print_parse_result(node->control.init, nest + 1);
      }
      if (node->control.condition)
      {
        make_space(nest);
        printf("|-cond:\n");
        _print_parse_result(node->control.condition, nest + 1);
      }
      if (node->control.true_code)
      {
        make_space(nest);
        printf("|-then:\n");
        _print_parse_result(node->control.true_code, nest + 1);
      }
      if (node->control.false_code)
      {
        make_space(nest);
        printf("|-else:\n");
        _print_parse_result(node->control.false_code, nest + 1);
      }
      if (node->control.update)
      {
        make_space(nest);
        printf("|-update:\n");
        _print_parse_result(node->control.update, nest + 1);
      }
      if (node->control.ternary_child)
      {  // For ternary
        make_space(nest);
        printf("|-chs:\n");
        _print_parse_result(node->control.ternary_child, nest + 1);
      }
      // For switch, case_list is handled separately in analyzer/generator, not
      // directly here.
      break;
    case ND_GOTO:
    case ND_LABEL:
      if (node->jump.statement_child)
      {
        _print_parse_result(node->jump.statement_child, nest + 1);
      }
      break;
    case ND_CASE:
      if (node->jump.is_case)
        printf(" %ld\n", node->jump.constant_expression);
      if (node->jump.statement_child)
      {
        _print_parse_result(node->jump.statement_child, nest + 1);
      }
      break;
      break;
    case ND_BLOCK:
      if (node->func.stmt)
      {  // ND_BLOCK uses func.stmt
        make_space(nest);
        printf("|-block:\n");
        for (NDBlock *p = node->func.stmt; p; p = p->next)
        {
          _print_parse_result(p->node, nest + 1);
        }
      }
      break;
    case ND_STRING:
      // No child nodes to print directly from Node structure
      break;
    case ND_INITIALIZER:
      if (node->initialize.init_list)
      {
        printf("\n");
        make_space(nest);
        printf("|-init_list:\n");
        for (size_t i = 1; i <= vector_size(node->initialize.init_list); i++)
        {
          _print_parse_result(vector_peek_at(node->initialize.init_list, i),
                              nest + 1);
        }
      }
      break;
    default: return;
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
      printf("--- node[%d] ---\n", i++);
      _print_parse_result(pointer->node, 0);
      printf("\n");
    }
  }
}

void _print_mermaid_result(Node *node, int *i, FILE *fp)
{
  if (!node)
    return;

  int current_node_id = (*i)++;
  fprintf(fp, "  id%d[%s];\n", current_node_id, nodekindlist[node->kind]);

  if (node->lhs)
  {
    int child_id = *i;
    fprintf(fp, "  id%d -- \"lhs\" --> id%d;\n", current_node_id, child_id);
    _print_mermaid_result(node->lhs, i, fp);
  }

  if (node->rhs)
  {
    int child_id = *i;
    fprintf(fp, "  id%d -- \"rhs\" --> id%d;\n", current_node_id, child_id);
    _print_mermaid_result(node->rhs, i, fp);
  }

  switch (node->kind)
  {
    case ND_FUNCDEF:
    case ND_FUNCCALL:
    case ND_BUILTINFUNC:
      if (node->func.expr)
      {
        for (size_t j = 1; j <= vector_size(node->func.expr); j++)
        {
          int child_id = *i;
          fprintf(fp, "  id%d -- \"arg%zu\" --> id%d;\n", current_node_id, j, child_id);
          _print_mermaid_result(vector_peek_at(node->func.expr, j), i, fp);
        }
      }
      if (node->func.stmt)
      {
        for (NDBlock *p = node->func.stmt; p; p = p->next)
        {
          int child_id = *i;
          fprintf(fp, "  id%d -- \"stmt\" --> id%d;\n", current_node_id, child_id);
          _print_mermaid_result(p->node, i, fp);
        }
      }
      break;
    case ND_IF:
    case ND_ELIF:
    case ND_FOR:
    case ND_WHILE:
    case ND_DO:
    case ND_TERNARY:
    case ND_LOGICAL_OR:
    case ND_LOGICAL_AND:
    case ND_SWITCH:
      if (node->control.init)
      {
        int child_id = *i;
        fprintf(fp, "  id%d -- \"init\" --> id%d;\n", current_node_id, child_id);
        _print_mermaid_result(node->control.init, i, fp);
      }
      if (node->control.condition)
      {
        int child_id = *i;
        fprintf(fp, "  id%d -- \"cond\" --> id%d;\n", current_node_id, child_id);
        _print_mermaid_result(node->control.condition, i, fp);
      }
      if (node->control.true_code)
      {
        int child_id = *i;
        fprintf(fp, "  id%d -- \"then\" --> id%d;\n", current_node_id, child_id);
        _print_mermaid_result(node->control.true_code, i, fp);
      }
      if (node->control.false_code)
      {
        int child_id = *i;
        fprintf(fp, "  id%d -- \"else\" --> id%d;\n", current_node_id, child_id);
        _print_mermaid_result(node->control.false_code, i, fp);
      }
      if (node->control.update)
      {
        int child_id = *i;
        fprintf(fp, "  id%d -- \"update\" --> id%d;\n", current_node_id, child_id);
        _print_mermaid_result(node->control.update, i, fp);
      }
      if (node->control.ternary_child)
      {
        int child_id = *i;
        fprintf(fp, "  id%d -- \"child\" --> id%d;\n", current_node_id, child_id);
        _print_mermaid_result(node->control.ternary_child, i, fp);
      }
      break;
    case ND_GOTO:
    case ND_LABEL:
      if (node->jump.statement_child)
      {
        int child_id = *i;
        fprintf(fp, "  id%d -- \"stmt\" --> id%d;\n", current_node_id, child_id);
        _print_mermaid_result(node->jump.statement_child, i, fp);
      }
      break;
    case ND_CASE:
      if (node->jump.statement_child)
      {
        int child_id = *i;
        fprintf(fp, "  id%d -- \"stmt\" --> id%d;\n", current_node_id, child_id);
        _print_mermaid_result(node->jump.statement_child, i, fp);
      }
      break;
    case ND_BLOCK:
      if (node->func.stmt)
      {
        for (NDBlock *p = node->func.stmt; p; p = p->next)
        {
          int child_id = *i;
          fprintf(fp, "  id%d -- \"stmt\" --> id%d;\n", current_node_id, child_id);
          _print_mermaid_result(p->node, i, fp);
        }
      }
      break;
    case ND_INITIALIZER:
      if (node->initialize.init_list)
      {
        for (size_t j = 1; j <= vector_size(node->initialize.init_list); j++)
        {
          int child_id = *i;
          fprintf(fp, "  id%d -- \"init%zu\" --> id%d;\n", current_node_id, j, child_id);
          _print_mermaid_result(vector_peek_at(node->initialize.init_list, j), i, fp);
        }
      }
      break;
    default:
      return;
  }
}

void print_mermaid_result(FuncBlock *node, char *output_file_name)
{
  FILE *fp = fopen(output_file_name, "w");
  if (!fp)
    error_exit("Failed to open file");

  fprintf(fp, "graph TD;\n");
  int i = 0;
  for (FuncBlock *pointer = node; pointer; pointer = pointer->next)
  {
    if (pointer->node && pointer->node->kind != ND_NOP)
    {
      _print_mermaid_result(pointer->node, &i, fp);
    }
  }

  fclose(fp);
}

void print_definition()
{
  pr_debug("Definition:");
  printf("Object-like macro:\n");
  for (size_t i = 1; i <= vector_size(object_like_macro_list); i++)
  {
    object_like_macro_storage *tmp = vector_peek_at(object_like_macro_list, i);
    printf("  %.*s: ", (int)tmp->identifier->len, tmp->identifier->str);
    for (size_t j = 1; j <= vector_size(tmp->token_string); j++)
    {
      Token *token = vector_peek_at(tmp->token_string, j);
      printf("%.*s ", (int)token->len, token->str);
    }
    printf("\n");
  }
  printf("Function-like macro:\n");
  for (size_t i = 1; i <= vector_size(function_like_macro_list); i++)
  {
    function_like_macro_storage *tmp =
        vector_peek_at(function_like_macro_list, i);
    printf("  %.*s(", (int)tmp->identifier->len, tmp->identifier->str);
    for (size_t j = 1; j <= vector_size(tmp->arguments); j++)
    {
      Token *token = vector_peek_at(tmp->arguments, j);
      printf("%.*s", (int)token->len, token->str);
      if (j < vector_size(tmp->arguments))
        printf(", ");
    }
    printf("): ");
    for (size_t j = 1; j <= vector_size(tmp->token_string); j++)
    {
      Token *token = vector_peek_at(tmp->token_string, j);
      printf("%.*s ", (int)token->len, token->str);
    }
    printf("\n");
  }
}
