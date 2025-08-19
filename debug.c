#include "include/debug.h"

#ifdef SELF_HOST
#include "test/compiler_header.h"
#else
#include <assert.h>
#include <stdio.h>
#include <string.h>
#endif

#include "include/analyzer.h"
#include "include/common.h"
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
  pr_debug("Start\n");
  for (size_t i = 1; i <= vector_size(vec); i++)
  {
    Token *token = vector_peek_at(vec, i);
    printf("%.*s", (int)token->len, token->str);
  }
  pr_debug("\nEnd");
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
      {
        make_space(nest);
        printf("|-block:\n");
        for (NDBlock *p = node->func.stmt; p; p = p->next)
        {
          _print_parse_result(p->node, nest + 1);
        }
      }
      break;
    case ND_TYPE_NAME: break;
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
      {
        make_space(nest);
        printf("|-chs:\n");
        _print_parse_result(node->control.ternary_child, nest + 1);
      }
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
      {
        make_space(nest);
        printf("|-block:\n");
        for (NDBlock *p = node->func.stmt; p; p = p->next)
        {
          _print_parse_result(p->node, nest + 1);
        }
      }
      break;
    case ND_STRING: break;
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
          fprintf(fp, "  id%d -- \"arg%zu\" --> id%d;\n", current_node_id, j,
                  child_id);
          _print_mermaid_result(vector_peek_at(node->func.expr, j), i, fp);
        }
      }
      if (node->func.stmt)
      {
        for (NDBlock *p = node->func.stmt; p; p = p->next)
        {
          int child_id = *i;
          fprintf(fp, "  id%d -- \"stmt\" --> id%d;\n", current_node_id,
                  child_id);
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
        fprintf(fp, "  id%d -- \"init\" --> id%d;\n", current_node_id,
                child_id);
        _print_mermaid_result(node->control.init, i, fp);
      }
      if (node->control.condition)
      {
        int child_id = *i;
        fprintf(fp, "  id%d -- \"cond\" --> id%d;\n", current_node_id,
                child_id);
        _print_mermaid_result(node->control.condition, i, fp);
      }
      if (node->control.true_code)
      {
        int child_id = *i;
        fprintf(fp, "  id%d -- \"then\" --> id%d;\n", current_node_id,
                child_id);
        _print_mermaid_result(node->control.true_code, i, fp);
      }
      if (node->control.false_code)
      {
        int child_id = *i;
        fprintf(fp, "  id%d -- \"else\" --> id%d;\n", current_node_id,
                child_id);
        _print_mermaid_result(node->control.false_code, i, fp);
      }
      if (node->control.update)
      {
        int child_id = *i;
        fprintf(fp, "  id%d -- \"update\" --> id%d;\n", current_node_id,
                child_id);
        _print_mermaid_result(node->control.update, i, fp);
      }
      if (node->control.ternary_child)
      {
        int child_id = *i;
        fprintf(fp, "  id%d -- \"child\" --> id%d;\n", current_node_id,
                child_id);
        _print_mermaid_result(node->control.ternary_child, i, fp);
      }
      break;
    case ND_GOTO:
    case ND_LABEL:
      if (node->jump.statement_child)
      {
        int child_id = *i;
        fprintf(fp, "  id%d -- \"stmt\" --> id%d;\n", current_node_id,
                child_id);
        _print_mermaid_result(node->jump.statement_child, i, fp);
      }
      break;
    case ND_CASE:
      if (node->jump.statement_child)
      {
        int child_id = *i;
        fprintf(fp, "  id%d -- \"stmt\" --> id%d;\n", current_node_id,
                child_id);
        _print_mermaid_result(node->jump.statement_child, i, fp);
      }
      break;
    case ND_BLOCK:
      if (node->func.stmt)
      {
        for (NDBlock *p = node->func.stmt; p; p = p->next)
        {
          int child_id = *i;
          fprintf(fp, "  id%d -- \"stmt\" --> id%d;\n", current_node_id,
                  child_id);
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
          fprintf(fp, "  id%d -- \"init%zu\" --> id%d;\n", current_node_id, j,
                  child_id);
          _print_mermaid_result(vector_peek_at(node->initialize.init_list, j),
                                i, fp);
        }
      }
      break;
    default: return;
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

static const char *get_size_prefix(size_t size)
{
  switch (size)
  {
    case 1: return "BYTE";
    case 2: return "WORD";
    case 4: return "DWORD";
    case 8: return "QWORD";
    default: return "";
  }
}

static void fprint_ir(FILE *fp, IR *ir, bool mermaid_escape)
{
  switch (ir->kind)
  {
    case IR_CALL:
    {
      fprintf(fp, "CALL r%d, %.*s, (", ir->call.dst_reg,
              (int)ir->call.func_name_size, ir->call.func_name);
      for (size_t k = 0; k < vector_size(ir->call.args); k++)
      {
        int *reg = vector_peek_at(ir->call.args, k + 1);
        fprintf(fp, "r%d%s", *reg,
                (k == vector_size(ir->call.args) - 1) ? "" : ", ");
      }
      fprintf(fp, ")");
      break;
    }
    case IR_FUNC_PROLOGUE: fprintf(fp, "PROLOGUE"); break;
    case IR_FUNC_EPILOGUE: fprintf(fp, "EPILOGUE"); break;
    case IR_RET: fprintf(fp, "RET r%d", ir->ret.src_reg); break;
    case IR_MOV:
      if (ir->mov.is_imm)
        fprintf(fp, "MOV r%d, %lld", ir->mov.dst_reg, ir->mov.imm_val);
      else
        fprintf(fp, "MOV r%d, r%d", ir->mov.dst_reg, ir->mov.src_reg);
      break;
    case IR_ADD:
      fprintf(fp, "ADD %s r%d, r%d, r%d", get_size_prefix(ir->bin_op.lhs_size),
              ir->bin_op.dst_reg, ir->bin_op.lhs_reg, ir->bin_op.rhs_reg);
      break;
    case IR_SUB:
      fprintf(fp, "SUB %s r%d, r%d, r%d", get_size_prefix(ir->bin_op.lhs_size),
              ir->bin_op.dst_reg, ir->bin_op.lhs_reg, ir->bin_op.rhs_reg);
      break;
    case IR_MUL:
      fprintf(fp, "MUL %s r%d, r%d, r%d", get_size_prefix(ir->bin_op.lhs_size),
              ir->bin_op.dst_reg, ir->bin_op.lhs_reg, ir->bin_op.rhs_reg);
      break;
    case IR_OP_DIV:
      fprintf(fp, "DIV %s r%d, r%d, r%d", get_size_prefix(ir->bin_op.lhs_size),
              ir->bin_op.dst_reg, ir->bin_op.lhs_reg, ir->bin_op.rhs_reg);
      break;
    case IR_OP_IDIV:
      fprintf(fp, "IDIV %s r%d, r%d, r%d", get_size_prefix(ir->bin_op.lhs_size),
              ir->bin_op.dst_reg, ir->bin_op.lhs_reg, ir->bin_op.rhs_reg);
      break;
    case IR_EQ:
      fprintf(fp, "EQ %s r%d, r%d, r%d", get_size_prefix(ir->bin_op.lhs_size),
              ir->bin_op.dst_reg, ir->bin_op.lhs_reg, ir->bin_op.rhs_reg);
      break;
    case IR_NEQ:
      fprintf(fp, "NEQ %s r%d, r%d, r%d", get_size_prefix(ir->bin_op.lhs_size),
              ir->bin_op.dst_reg, ir->bin_op.lhs_reg, ir->bin_op.rhs_reg);
      break;
    case IR_LT:
      fprintf(fp, "LT %s r%d, r%d, r%d", get_size_prefix(ir->bin_op.lhs_size),
              ir->bin_op.dst_reg, ir->bin_op.lhs_reg, ir->bin_op.rhs_reg);
      break;
    case IR_LTE:
      fprintf(fp, "LTE %s r%d, r%d, r%d", get_size_prefix(ir->bin_op.lhs_size),
              ir->bin_op.dst_reg, ir->bin_op.lhs_reg, ir->bin_op.rhs_reg);
      break;
    case IR_OR:
      fprintf(fp, "OR %s r%d, r%d, r%d", get_size_prefix(ir->bin_op.lhs_size),
              ir->bin_op.dst_reg, ir->bin_op.lhs_reg, ir->bin_op.rhs_reg);
      break;
    case IR_XOR:
      fprintf(fp, "XOR %s r%d, r%d, r%d", get_size_prefix(ir->bin_op.lhs_size),
              ir->bin_op.dst_reg, ir->bin_op.lhs_reg, ir->bin_op.rhs_reg);
      break;
    case IR_AND:
      fprintf(fp, "AND %s r%d, r%d, r%d", get_size_prefix(ir->bin_op.lhs_size),
              ir->bin_op.dst_reg, ir->bin_op.lhs_reg, ir->bin_op.rhs_reg);
      break;
    case IR_SHL:
      fprintf(fp, "SHL %s r%d, r%d, r%d", get_size_prefix(ir->bin_op.lhs_size),
              ir->bin_op.dst_reg, ir->bin_op.lhs_reg, ir->bin_op.rhs_reg);
      break;
    case IR_SHR:
      fprintf(fp, "SHR %s r%d, r%d, r%d", get_size_prefix(ir->bin_op.lhs_size),
              ir->bin_op.dst_reg, ir->bin_op.lhs_reg, ir->bin_op.rhs_reg);
      break;
    case IR_SAL:
      fprintf(fp, "SAL %s r%d, r%d, r%d", get_size_prefix(ir->bin_op.lhs_size),
              ir->bin_op.dst_reg, ir->bin_op.lhs_reg, ir->bin_op.rhs_reg);
      break;
    case IR_SAR:
      fprintf(fp, "SAR %s r%d, r%d, r%d", get_size_prefix(ir->bin_op.lhs_size),
              ir->bin_op.dst_reg, ir->bin_op.lhs_reg, ir->bin_op.rhs_reg);
      break;
    case IR_BIT_NOT:
      fprintf(fp, "BNOT r%d, r%d", ir->un_op.dst_reg, ir->un_op.src_reg);
      break;
    case IR_JMP: fprintf(fp, "JMP %s", ir->jmp.label); break;
    case IR_JNE:
      fprintf(fp, "JNE %s, r%d", ir->jmp.label, ir->jmp.cond_reg);
      break;
    case IR_JE:
      fprintf(fp, "JE %s, r%d", ir->jmp.label, ir->jmp.cond_reg);
      break;
    case IR_LOAD:
      fprintf(fp, "LOAD %s r%d, [r%d + %d]", get_size_prefix(ir->mem.size),
              ir->mem.reg, ir->mem.mem_reg, ir->mem.offset);
      break;
    case IR_STORE:
      fprintf(fp, "STORE %s [r%d + %d], r%d", get_size_prefix(ir->mem.size),
              ir->mem.mem_reg, ir->mem.offset, ir->mem.reg);
      break;
    case IR_STORE_ARG:
      fprintf(fp, "STORE_ARG %s r%d, %d", get_size_prefix(ir->store_arg.size),
              ir->store_arg.dst_reg, ir->store_arg.arg_index);
      break;
    case IR_LEA:
      if (ir->lea.is_local)
        fprintf(fp, "LEA r%d, LOCAL %zu", ir->lea.dst_reg, ir->lea.var_offset);
      else if (ir->lea.is_static)
        fprintf(fp, "LEA r%d, STATIC %.*s", ir->lea.dst_reg,
                (int)ir->lea.var_name_len, ir->lea.var_name);
      else
        fprintf(fp, "LEA r%d, GLOBAL %.*s", ir->lea.dst_reg,
                (int)ir->lea.var_name_len, ir->lea.var_name);
      break;
    case IR_LABEL: fprintf(fp, "%s:", ir->label.name); break;
    case IR_NEG:
      fprintf(fp, "NEG r%d, r%d", ir->un_op.dst_reg, ir->un_op.src_reg);
      break;
    case IR_NOT:
      fprintf(fp, "NOT r%d, r%d", ir->un_op.dst_reg, ir->un_op.src_reg);
      break;
    case IR_BUILTIN_ASM:
      if (mermaid_escape)
        fprintf(fp, "ASM #%.*s#", (int)ir->builtin_asm.asm_len,
                ir->builtin_asm.asm_str);
      else
        fprintf(fp, "ASM \"%.*s\"", (int)ir->builtin_asm.asm_len,
                ir->builtin_asm.asm_str);
      break;
    default: fprintf(fp, "unimplemented IR"); break;
  }
}

void dump_ir_fp(IRProgram *program, FILE *fp)
{
  // Dump global variables
  for (size_t i = 0; i < vector_size(program->global_vars); i++)
  {
    GlobalVar *gvar = vector_peek_at(program->global_vars, i + 1);
    fprintf(fp, "GVAR %.*s %zu", (int)gvar->var_name_len, gvar->var_name,
            gvar->var_size);
    for (size_t j = 1; j <= vector_size(gvar->initializer->IRs); j++)
    {
      GVarInitializer *init = vector_peek_at(gvar->initializer->IRs, j);
      switch (init->how2_init)
      {
        case init_zero: fprintf(fp, " ZERO %lu\n", init->zero_len); break;
        case init_val:
          fprintf(fp, " VAL %lu %lld\n", init->value.value_size,
                  init->value.init_val);
          break;
        case init_pointer:
          fprintf(fp, " VAR %.*s\n", (int)init->assigned_var.var_name_len,
                  init->assigned_var.var_name);
          break;
        case init_string:
          fprintf(fp, " STRING %s\n", init->literal_name);
          break;
        default: unreachable(); break;
      }
    }
  }

  // Dump string literals
  for (size_t i = 0; i < vector_size(program->strings); i++)
  {
    Var *str_var = vector_peek_at(program->strings, i + 1);
    fprintf(fp, "STRING %.*s \"%.*s\"\n", (int)str_var->len, str_var->name,
            (int)str_var->token->len, str_var->token->str);
  }

  // Loop through all functions
  for (size_t i = 0; i < vector_size(program->functions); i++)
  {
    IRFunc *func = vector_peek_at(program->functions, i + 1);
    if (func->builtin_func == FUNC_USER_DEFINED)
    {
      // Print function information
      fprintf(fp, "FUNC %.*s %zu %zu %d\n",
              (int)func->user_defined.function_name_size,
              func->user_defined.function_name, func->user_defined.stack_size,
              func->user_defined.num_virtual_regs,
              func->user_defined.is_static);
    }
    // Loop through the IR blocks of the function
    for (size_t j = 0; j < vector_size(func->IR_Blocks); j++)
    {
      IR_Blocks *irs = vector_peek_at(func->IR_Blocks, j + 1);
      for (size_t k = 0; k < vector_size(irs->IRs); k++)
      {
        IR *ir = vector_peek_at(irs->IRs, k + 1);
        fprintf(fp, "  ");
        fprint_ir(fp, ir, false);
        fprintf(fp, "\n");
      }
    }
  }
}

void dump_ir(IRProgram *program, char *path)
{
  FILE *fp = fopen(path, "w");
  if (!fp)
  {
    error_exit("Failed to open file for writing: %s", path);
  }
  dump_ir_fp(program, fp);
  fclose(fp);
}

void dump_ir_stdout(IRProgram *program)
{
  dump_ir_fp(program, stdout);
}

static int get_block_index(Vector *blocks, IR_Blocks *target_block)
{
  for (size_t i = 1; i <= vector_size(blocks); i++)
  {
    if (vector_peek_at(blocks, i) == target_block)
    {
      return i - 1;
    }
  }
  return -1;  // Not found
}

void dump_cfg(IRProgram *program, FILE *fp)
{
  for (size_t i = 1; i <= vector_size(program->functions); i++)
  {
    IRFunc *func = vector_peek_at(program->functions, i);
    if (func->builtin_func != FUNC_USER_DEFINED)
      continue;

    fprintf(fp, "CFG for function %.*s:\n",
            (int)func->user_defined.function_name_size,
            func->user_defined.function_name);
    fprintf(fp, "graph TD\n");

    // Define all nodes
    for (size_t j = 1; j <= vector_size(func->IR_Blocks); j++)
    {
      IR_Blocks *block = vector_peek_at(func->IR_Blocks, j);
      int block_id = j - 1;

      fprintf(fp, "  B%d[\"<b>B%d</b><br/>", block_id, block_id);

      for (size_t k = 1; k <= vector_size(block->IRs); k++)
      {
        IR *ir = vector_peek_at(block->IRs, k);
        fprint_ir(fp, ir, true);
        fprintf(fp, "<br/>");
      }
      fprintf(fp, "\"]\n");
    }

    // Define all edges
    for (size_t j = 1; j <= vector_size(func->IR_Blocks); j++)
    {
      IR_Blocks *block = vector_peek_at(func->IR_Blocks, j);
      int block_id = j - 1;

      if (block->lhs)
      {
        int lhs_id = get_block_index(func->IR_Blocks, block->lhs);
        if (lhs_id != -1)
          fprintf(fp, "  B%d --> B%d\n", block_id, lhs_id);
      }
      if (block->rhs)
      {
        int rhs_id = get_block_index(func->IR_Blocks, block->rhs);
        if (rhs_id != -1)
          fprintf(fp, "  B%d --> B%d\n", block_id, rhs_id);
      }
    }
    fprintf(fp, "\n");
  }
}
