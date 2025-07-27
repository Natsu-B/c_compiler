// ------------------------------------------------------------------------------------
// Intermediate Representation Generator
// ------------------------------------------------------------------------------------

#include "include/ir_generator.h"

#include "include/common.h"
#ifdef SELF_HOST
#include "test/compiler_header.h"
#else
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#endif

#include "include/error.h"
#include "include/parser.h"
#include "include/vector.h"

#if DEBUG
// Debug output macro
#define output_debug(fmt, ...) \
  printf("# %s:%d:%s() " fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__);
#else
#define output_debug(fmt, ...)
#endif

// Virtual register ID
static int reg_id;
// Generates a new virtual register and returns its ID.
static int *gen_reg()
{
  int *new_reg = malloc(sizeof(int));
  *new_reg = reg_id++;
  output_debug("gen_reg: new reg_id = %d", *new_reg);
  return new_reg;
}

// Label ID
static int label_id;
// Generates a new label name (e.g., .L0, .L1, ...).
static char *gen_label()
{
  char buf[20];
  sprintf(buf, ".L%d", label_id++);
  char *name = malloc(strlen(buf) + 1);
  strcpy(name, buf);
  return name;
}

// Forward declarations
static int *gen_addr(Vector *v, Node *node);
static int *gen_stmt(Vector *v, Node *node);
static void dump_ir_fp(IRProgram *program, FILE *fp);

IRFunc *call_builtin_func(Node *node)
{
  IRFunc *func = calloc(1, sizeof(IRFunc));
  func->builtin_func = FUNC_ASM;
  func->IR_Blocks = vector_new();
  gen_stmt(func->IR_Blocks, node);
  if (vector_size(func->IR_Blocks) != 1)
    unreachable();
  return func;
}

IRProgram *gen_ir(FuncBlock *parsed)
{
  IRProgram *program = calloc(1, sizeof(IRProgram));
  program->functions = vector_new();
  program->global_vars = vector_new();

  // Loop through all functions/global variables
  for (FuncBlock *fb = parsed; fb; fb = fb->next)
  {
    if (!fb->node)
      continue;
    switch (fb->node->kind)
    {
      case ND_NOP: break;
      case ND_FUNCDEF:
      {
        // Case: Function definition
        IRFunc *func = calloc(1, sizeof(IRFunc));
        func->user_defined.function_name = fb->node->token->str;
        func->user_defined.function_name_size = fb->node->token->len;
        func->user_defined.is_static =
            fb->node->func.storage_class_specifier & 1 << 2;
        func->IR_Blocks = vector_new();

        reg_id = 0;  // Reset register ID for each function
        gen_stmt(func->IR_Blocks,
                 fb->node);  // Generate IR for statements

        func->user_defined.stack_size = fb->stacksize;
        func->user_defined.num_virtual_regs = reg_id;
        vector_push(program->functions, func);
        break;
      }
      case ND_BUILTINFUNC:
      {
        vector_push(program->functions, call_builtin_func(fb->node));
        break;
      }
      case ND_VAR:
      {
        if (fb->node->variable.var->storage_class_specifier & 1 << 1)
          continue;  // extern storage specifier
        GlobalVar *gvar = calloc(1, sizeof(GlobalVar));
        gvar->var_name = fb->node->variable.var->name;
        gvar->var_name_len = fb->node->variable.var->len;
        gvar->var_size = size_of(fb->node->variable.var->type);
        gvar->how2_init = init_zero;  // Default to zero-initialization
        gvar->is_static =
            fb->node->variable.var->storage_class_specifier & 1 << 2;
        vector_push(program->global_vars, gvar);
        break;
      }
      case ND_ASSIGN:
      {
        GlobalVar *gvar = calloc(1, sizeof(GlobalVar));
        gvar->var_name = fb->node->lhs->variable.var->name;
        gvar->var_name_len = fb->node->lhs->variable.var->len;
        gvar->var_size = size_of(fb->node->lhs->variable.var->type);
        gvar->is_static =
            fb->node->lhs->variable.var->storage_class_specifier & 1 << 2;
        switch (fb->node->rhs->kind)
        {
          case ND_NUM:
            gvar->how2_init = init_val;
            gvar->init_val = fb->node->rhs->num_val;
            break;
          case ND_ADDR:
            gvar->how2_init = init_pointer;
            gvar->assigned_var.var_name =
                fb->node->rhs->lhs->variable.var->name;
            gvar->assigned_var.var_name_len =
                fb->node->rhs->lhs->variable.var->len;
            break;
          case ND_STRING:
            gvar->how2_init = init_string;
            gvar->literal_name = fb->node->rhs->literal_name;
            break;
          default: unreachable(); break;
        }
        vector_push(program->global_vars, gvar);
        break;
      }
      default: unreachable(); break;
    }
  }

  program->strings = get_string_list();

#ifdef DEBUG
  // If it's a debug build, dump the generated IR to stdout.
  dump_ir_fp(program, stdout);
#endif

  return program;
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

static int *gen_assign(Vector *v, Node *assigned, Node *node, size_t padding,
                       size_t assign_size)
{
  if (node->kind == ND_INITIALIZER)
  {
    for (size_t i = 0; i < vector_size(node->initialize.init_list); i++)
    {
      Node *child = vector_peek_at(node->initialize.init_list, i + 1);
      gen_assign(v, assigned, child, padding + i * size_of(node->type->ptr_to),
                 size_of(child->type));
    }
    // Uninitialized array elements are padded with zeros
    size_t done_init =
        vector_size(node->initialize.init_list) * size_of(node->type->ptr_to);
    while (size_of(node->type) - done_init)
    {
      // int *lhs_ptr = gen_addr(v, assigned);
      // IR *ir = calloc(1, sizeof(IR));
      // ir->kind = IR_ADD;
      // ir->bin_op.lhs_reg = *lhs_ptr;
      // ir->bin_op.rhs_reg = *gen_stmt(v, new_node_num(padding + done_init));
      // ir->bin_op.lhs_size = 8;  // *ptr
      // ir->bin_op.rhs_size = 8;
      // int *dst_ptr = gen_reg();
      // ir->bin_op.dst_reg = *dst_ptr;
      // vector_push(v, ir);
      size_t size = size_of(node->type) - done_init > 8
                        ? 8
                        : size_of(node->type) - done_init;
      int *zero_reg = gen_stmt(v, new_node_num(0));
      int *lhs_addr_ptr = gen_addr(v, assigned);
      IR *ir = calloc(1, sizeof(IR));
      ir->kind = IR_STORE;
      ir->mem.reg = *zero_reg;
      ir->mem.mem_reg = *lhs_addr_ptr;
      ir->mem.offset = padding + done_init;
      ir->mem.size = size;
      vector_push(v, ir);
      done_init += size;
    }
    return NULL;
  }
  else
  {
    // For an assignment, evaluate the RHS, then STORE it at the address of
    // the LHS.
    int *rhs_ptr = gen_stmt(v, node);
    int *lhs_addr_ptr = gen_addr(v, assigned);
    // if (padding)
    // {
    //   IR *ir = calloc(1, sizeof(IR));
    //   ir->kind = IR_ADD;
    //   ir->bin_op.lhs_reg = *rhs_ptr;
    //   ir->bin_op.rhs_reg = *gen_stmt(v, new_node_num(padding));
    //   ir->bin_op.lhs_size = 8;  // *ptr
    //   ir->bin_op.rhs_size = 8;
    //   rhs_ptr = gen_reg();
    //   ir->bin_op.dst_reg = *rhs_ptr;
    //   vector_push(v, ir);
    // }
    IR *ir = calloc(1, sizeof(IR));
    ir->kind = IR_STORE;
    ir->mem.reg = *rhs_ptr;
    ir->mem.mem_reg = *lhs_addr_ptr;
    ir->mem.offset = padding;
    ir->mem.size = assign_size;
    vector_push(v, ir);
    return rhs_ptr;  // The result of an assignment expression is the RHS
                     // value.
  }
}

static int *gen_addr(Vector *v, Node *node)
{
  switch (node->kind)
  {
    case ND_VAR:
    {
      // For a variable, generate a LEA (Load Effective Address) instruction.
      IR *ir = calloc(1, sizeof(IR));
      ir->kind = IR_LEA;
      ir->lea.is_local = node->variable.var->is_local &&
                         !(node->variable.var->storage_class_specifier &
                           ((1 << 1) + (1 << 2)));
      ir->lea.is_static = node->variable.var->storage_class_specifier & 1 << 2;
      if (!ir->lea.is_local)
      {
        ir->lea.var_name = node->variable.var->name;
        ir->lea.var_name_len = node->variable.var->len;
      }
      else
        ir->lea.var_offset = node->variable.var->offset;
      int *dst_reg_ptr = gen_reg();
      ir->lea.dst_reg = *dst_reg_ptr;
      output_debug("gen_addr: LEA var name = %.*s", (int)ir->lea.var->len,
                   ir->lea.var->name);
      vector_push(v, ir);
      return dst_reg_ptr;
    }
    case ND_DEREF:
    {
      // For a dereference, evaluate the expression on the left of `*` to get
      // the address.
      return gen_stmt(v, node->lhs);
    }
    case ND_DOT:
    case ND_ARROW:
    {
      int *lhs_addr = node->kind == ND_DOT ? gen_addr(v, node->lhs)
                                           : gen_stmt(v, node->lhs);
      int *offset_val = gen_stmt(v, new_node_num(node->child_offset));

      IR *ir = calloc(1, sizeof(IR));
      ir->kind = IR_ADD;
      ir->bin_op.lhs_reg = *lhs_addr;
      ir->bin_op.rhs_reg = *offset_val;
      int *dst_reg = gen_reg();
      ir->bin_op.dst_reg = *dst_reg;
      vector_push(v, ir);

      return dst_reg;
    }
    default: error_exit("not an lvalue"); return NULL;  // Unreachable
  }
}

static int *gen_stmt(Vector *v, Node *node)
{
  if (!node || node->kind == ND_NOP)
    return NULL;
  switch (node->kind)
  {
    case ND_NUM:
    {
      // For a numeric literal, generate a MOV instruction.
      IR *ir = calloc(1, sizeof(IR));
      ir->kind = IR_MOV;
      ir->mov.is_imm = true;
      ir->mov.imm_val = node->num_val;
      int *dst_reg_ptr = gen_reg();
      ir->mov.dst_reg = *dst_reg_ptr;
      vector_push(v, ir);
      return dst_reg_ptr;
    }
    case ND_DOT:
    case ND_ARROW:
    case ND_VAR:
    {
      // If it's an array type, return its address.
      if (node->type->type == TYPE_ARRAY)
      {
        return gen_addr(v, node);
      }
      // For a variable, get its address and then generate a LOAD instruction.
      int *addr_reg_ptr = gen_addr(v, node);
      IR *ir = calloc(1, sizeof(IR));
      ir->kind = IR_LOAD;
      ir->mem.mem_reg = *addr_reg_ptr;
      ir->mem.offset = 0;
      int *reg_ptr = gen_reg();
      ir->mem.reg = *reg_ptr;
      ir->mem.size = size_of(node->type);
      vector_push(v, ir);
      return reg_ptr;
    }
    case ND_STRING:
    {
      // For a string literal, add it to the global string list and then
      // generate a LEA instruction to get its address.
      IR *ir = calloc(1, sizeof(IR));
      ir->kind = IR_LEA;
      ir->lea.is_local = false;
      ir->lea.is_static = true;
      ir->lea.var_name = node->literal_name;
      if (!node->literal_name)
        unreachable();
      ir->lea.var_name_len = strlen(node->literal_name);
      int *dst_reg_ptr = gen_reg();
      ir->lea.dst_reg = *dst_reg_ptr;
      vector_push(v, ir);
      return dst_reg_ptr;
    }
    case ND_ASSIGN:
    {
      return gen_assign(v, node->lhs, node->rhs, 0, size_of(node->type));
    }
    case ND_RETURN:
    {
      // For a return statement, evaluate the return value and generate a RET
      // instruction.
      int *src_ptr = gen_stmt(v, node->lhs);
      IR *ir = calloc(1, sizeof(IR));
      ir->kind = IR_RET;
      ir->ret.src_reg = *src_ptr;
      vector_push(v, ir);
      return NULL;  // A return statement does not produce a value.
    }
    case ND_IF:
    case ND_ELIF:
    {
      char *else_label = gen_label();
      char *end_label = gen_label();

      // Evaluate the condition.
      int *cond_reg_ptr = gen_stmt(v, node->control.condition);
      // If the condition is false (0), jump to else_label.
      IR *ir = calloc(1, sizeof(IR));
      ir->kind = IR_JE;  // Jump if Equal (to zero)
      ir->jmp.label = else_label;
      ir->jmp.cond_reg = *cond_reg_ptr;
      vector_push(v, ir);

      // Generate code for the 'true' block.
      gen_stmt(v, node->control.true_code);
      // Jump to the end_label.
      IR *jmp_ir = calloc(1, sizeof(IR));
      jmp_ir->kind = IR_JMP;
      jmp_ir->jmp.label = end_label;
      vector_push(v, jmp_ir);

      // Place the 'else' label.
      IR *else_label_ir = calloc(1, sizeof(IR));
      else_label_ir->kind = IR_LABEL;
      else_label_ir->label.name = else_label;
      vector_push(v, else_label_ir);

      // If there is a 'false' block (else, else if), generate its code.
      if (node->control.false_code)
      {
        gen_stmt(v, node->control.false_code);
      }

      // Place the 'end' label.
      IR *end_label_ir = calloc(1, sizeof(IR));
      end_label_ir->kind = IR_LABEL;
      end_label_ir->label.name = end_label;
      vector_push(v, end_label_ir);
      return NULL;  // Control flow statements do not produce a value.
    }
    case ND_WHILE:
    {
      // Generate label
      size_t label_len = 13 + node->control.label->len;
      char *begin_label = malloc(label_len);
      char *end_label = malloc(label_len);
      snprintf(begin_label, label_len, ".Lbeginwhile%.*s",
               (int)node->control.label->len, node->control.label->name);
      snprintf(end_label, label_len, ".Lendwhile%.*s",
               (int)node->control.label->len, node->control.label->name);

      // Place the loop start label.
      IR *begin_label_ir = calloc(1, sizeof(IR));
      begin_label_ir->kind = IR_LABEL;
      begin_label_ir->label.name = begin_label;
      vector_push(v, begin_label_ir);

      // Evaluate the condition.
      int *cond_reg_ptr = gen_stmt(v, node->control.condition);
      // If the condition is false (0), jump to end_label.
      IR *ir = calloc(1, sizeof(IR));
      ir->kind = IR_JE;
      ir->jmp.label = end_label;
      ir->jmp.cond_reg = *cond_reg_ptr;
      vector_push(v, ir);

      // Generate code for the loop body.
      gen_stmt(v, node->control.true_code);
      // Jump to the loop start label.
      IR *jmp_ir = calloc(1, sizeof(IR));
      jmp_ir->kind = IR_JMP;
      jmp_ir->jmp.label = begin_label;
      vector_push(v, jmp_ir);

      // Place the loop end label.
      IR *end_label_ir = calloc(1, sizeof(IR));
      end_label_ir->kind = IR_LABEL;
      end_label_ir->label.name = end_label;
      vector_push(v, end_label_ir);
      return NULL;
    }
    case ND_DO:
    {
      char *label_name = malloc(10 + node->control.label->len);
      snprintf(label_name, 10 + node->control.label->len, ".Lbegindo%.*s",
               (int)node->control.label->len, node->control.label->name);
      IR *label = calloc(1, sizeof(IR));
      label->kind = IR_LABEL;
      label->label.name = label_name;
      vector_push(v, label);
      gen_stmt(v, node->control.true_code);
      int *cond_ptr = gen_stmt(v, node->control.condition);
      IR *condition = calloc(1, sizeof(IR));
      condition->kind = IR_JNE;
      condition->jmp.label = label_name;
      condition->jmp.cond_reg = *cond_ptr;
      vector_push(v, condition);
      char *end_name = malloc(8 + node->control.label->len);
      snprintf(end_name, 8 + node->control.label->len, ".Lenddo%.*s",
               (int)node->control.label->len, node->control.label->name);
      IR *end_label = calloc(1, sizeof(IR));
      end_label->kind = IR_LABEL;
      end_label->label.name = end_name;
      vector_push(v, end_label);
      return NULL;
    }
    case ND_FOR:
    {
      // Generate label
      size_t label_len = 12 + node->control.label->len;
      char *begin_label = malloc(label_len);
      char *end_label = malloc(label_len);
      snprintf(begin_label, label_len, ".Lbeginfor%.*s",
               (int)node->control.label->len, node->control.label->name);
      snprintf(end_label, label_len, ".Lendfor%.*s",
               (int)node->control.label->len, node->control.label->name);

      // Generate the initializer if it exists.
      if (node->control.init)
        gen_stmt(v, node->control.init);

      // Place the loop start label.
      IR *begin_label_ir = calloc(1, sizeof(IR));
      begin_label_ir->kind = IR_LABEL;
      begin_label_ir->label.name = begin_label;
      vector_push(v, begin_label_ir);

      // If there is a condition, evaluate it and exit the loop if false.
      if (node->control.condition)
      {
        int *cond_reg_ptr = gen_stmt(v, node->control.condition);
        IR *ir = calloc(1, sizeof(IR));
        ir->kind = IR_JE;
        ir->jmp.label = end_label;
        ir->jmp.cond_reg = *cond_reg_ptr;
        vector_push(v, ir);
      }

      // Generate code for the loop body.
      gen_stmt(v, node->control.true_code);
      // Generate the update expression if it exists.
      if (node->control.update)
        gen_stmt(v, node->control.update);

      // Jump to the loop start label.
      IR *jmp_ir = calloc(1, sizeof(IR));
      jmp_ir->kind = IR_JMP;
      jmp_ir->jmp.label = begin_label;
      vector_push(v, jmp_ir);

      // Place the loop end label.
      IR *end_label_ir = calloc(1, sizeof(IR));
      end_label_ir->kind = IR_LABEL;
      end_label_ir->label.name = end_label;
      vector_push(v, end_label_ir);
      return NULL;
    }
    case ND_TERNARY:
    {
      char *false_label = gen_label();
      char *end_label = gen_label();
      int *condition = gen_stmt(v, node->lhs);
      IR *jump = calloc(1, sizeof(IR));
      jump->kind = IR_JE;
      jump->jmp.label = false_label;
      jump->jmp.cond_reg = *condition;
      vector_push(v, jump);
      int *true_reg = gen_stmt(v, node->control.ternary_child);
      IR *jump_end = calloc(1, sizeof(IR));
      jump_end->kind = IR_JMP;
      jump_end->jmp.label = end_label;
      vector_push(v, jump_end);
      IR *label_false = calloc(1, sizeof(IR));
      label_false->kind = IR_LABEL;
      label_false->label.name = false_label;
      vector_push(v, label_false);
      int *false_reg = gen_stmt(v, node->rhs);
      IR *merge = calloc(1, sizeof(IR));
      merge->kind = IR_MOV;
      merge->mov.src_reg = *false_reg;
      merge->mov.dst_reg = *true_reg;
      vector_push(v, merge);
      IR *label_end = calloc(1, sizeof(IR));
      label_end->kind = IR_LABEL;
      label_end->label.name = end_label;
      vector_push(v, label_end);
      return true_reg;
    }
    case ND_CASE:
    {
      char *label_name = malloc(14 + node->jump.switch_name->len);
      snprintf(label_name, 14 + node->jump.switch_name->len, ".Lswitch%.*s_%lu",
               (int)node->jump.switch_name->len, node->jump.switch_name->name,
               node->jump.case_num);
      IR *label = calloc(1, sizeof(IR));
      label->kind = IR_LABEL;
      label->label.name = label_name;
      vector_push(v, label);
      return gen_stmt(v, node->jump.statement_child);
    }
    case ND_SWITCH:
    {
      int *cond_ptr = gen_stmt(v, node->control.condition);
      GTLabel *switch_label = node->control.label;
      for (size_t i = 1; i <= vector_size(node->control.case_list); i++)
      {
        char *label_name = malloc(14 + switch_label->len);
        snprintf(label_name, 14 + switch_label->len, ".Lswitch%.*s_%lu",
                 (int)switch_label->len, switch_label->name, i - 1);
        Node *case_child = vector_peek_at(node->control.case_list, i);
        if (case_child->jump.is_case)
        {
          IR *cmp = calloc(1, sizeof(IR));
          cmp->kind = IR_EQ;
          cmp->bin_op.lhs_reg = *cond_ptr;
          cmp->bin_op.rhs_reg =
              *gen_stmt(v, new_node_num(case_child->jump.constant_expression));
          int *cmp_result = gen_reg();
          cmp->bin_op.dst_reg = *cmp_result;
          vector_push(v, cmp);
          IR *jump = calloc(1, sizeof(IR));
          jump->kind = IR_JNE;
          jump->jmp.label = label_name;
          jump->jmp.cond_reg = *cmp_result;
          vector_push(v, jump);
        }
        else
        {
          IR *jump = calloc(1, sizeof(IR));
          jump->kind = IR_JMP;
          jump->jmp.label = label_name;
          vector_push(v, jump);
        }
      }
      char *label_name = malloc(12 + switch_label->len);
      snprintf(label_name, 12 + switch_label->len, ".Lendswitch%.*s",
               (int)switch_label->len, switch_label->name);
      IR *jump = calloc(1, sizeof(IR));
      jump->kind = IR_JMP;
      jump->jmp.label = label_name;
      gen_stmt(v, node->control.true_code);
      IR *end = calloc(1, sizeof(IR));
      end->kind = IR_LABEL;
      end->label.name = label_name;
      vector_push(v, end);
      return NULL;
    }
    case ND_FUNCCALL:
    {
      // Evaluate each argument expression.
      Vector *args = vector_new();
      for (size_t i = 0; i < vector_size(node->func.expr); i++)
      {
        Node *arg = vector_peek_at(node->func.expr, i + 1);
        int *reg_val = gen_stmt(v, arg);
        vector_push(args, reg_val);
      }

      // Generate a CALL instruction.
      IR *ir = calloc(1, sizeof(IR));
      ir->kind = IR_CALL;
      ir->call.func_name = node->token->str;
      ir->call.func_name_size = node->token->len;
      ir->call.args = args;
      int *dst_reg_ptr = gen_reg();  // Register to receive the return value.
      ir->call.dst_reg = *dst_reg_ptr;
      vector_push(v, ir);
      return dst_reg_ptr;
    }
    case ND_DISCARD_EXPR:
    {
      // Expression statement (discard the evaluation result).
      gen_stmt(v, node->lhs);
      return NULL;
    }
    case ND_BLOCK:
    {
      // Generate code for each statement in the block sequentially.
      for (NDBlock *b = node->func.stmt; b; b = b->next)
      {
        gen_stmt(v, b->node);
      }
      return NULL;
    }
    case ND_FUNCDEF:
    {
      // Generate the function prologue.
      IR *prologue_ir = calloc(1, sizeof(IR));
      prologue_ir->kind = IR_FUNC_PROLOGUE;
      vector_push(v, prologue_ir);

      // Store arguments from registers to the variable area on the stack.
      for (size_t i = 0; i < vector_size(node->func.expr); i++)
      {
        Node *arg_node = vector_peek_at(node->func.expr, i + 1);
        int *addr_reg = gen_addr(v, arg_node);
        IR *ir = calloc(1, sizeof(IR));
        ir->kind = IR_STORE_ARG;
        ir->store_arg.dst_reg = *addr_reg;
        ir->store_arg.arg_index = i;
        ir->store_arg.size = size_of(arg_node->type);
        vector_push(v, ir);
      }

      // Generate code for the function body.
      for (NDBlock *b = node->func.stmt; b; b = b->next)
      {
        gen_stmt(v, b->node);
      }

      // Generate the function epilogue.
      IR *epilogue_ir = calloc(1, sizeof(IR));
      epilogue_ir->kind = IR_FUNC_EPILOGUE;
      vector_push(v, epilogue_ir);
      return NULL;
    }
    case ND_DEREF:
    {
      // Dereference `*p`.
      // Evaluate the address of p.
      int *addr_reg_ptr = gen_stmt(v, node->lhs);
      // Load the value from that address.
      IR *ir = calloc(1, sizeof(IR));
      ir->kind = IR_LOAD;
      ir->mem.mem_reg = *addr_reg_ptr;
      ir->mem.offset = 0;
      int *reg_ptr = gen_reg();
      ir->mem.reg = *reg_ptr;
      ir->mem.size = size_of(node->type);
      vector_push(v, ir);
      return reg_ptr;
    }
    case ND_ADDR:
    {
      // Address-of `&v`.
      return gen_addr(v, node->lhs);
    }
    case ND_LOGICAL_NOT:
    case ND_NOT:
    case ND_UNARY_MINUS:
    {
      int *src_reg_ptr = gen_stmt(v, node->lhs);
      IR *ir = calloc(1, sizeof(IR));
      switch (node->kind)
      {
        case ND_LOGICAL_NOT: ir->kind = IR_NOT; break;
        case ND_NOT: ir->kind = IR_BIT_NOT; break;
        case ND_UNARY_MINUS: ir->kind = IR_NEG; break;
        default: unreachable(); break;
      }
      ir->un_op.src_reg = *src_reg_ptr;
      int *dst_reg_ptr = gen_reg();
      ir->un_op.dst_reg = *dst_reg_ptr;
      vector_push(v, ir);
      return dst_reg_ptr;
    }
    case ND_UNARY_PLUS:
    {
      // Unary plus `+x` does nothing.
      return gen_stmt(v, node->lhs);
    }
    case ND_EVAL:
    {
      // Convert int to bool
      IR *ir = calloc(1, sizeof(IR));
      ir->kind = IR_NEQ;
      ir->bin_op.lhs_reg = *gen_stmt(v, node->lhs);
      ir->bin_op.rhs_reg = *gen_stmt(v, new_node_num(0));
      int *dst_reg_ptr = gen_reg();
      ir->bin_op.dst_reg = *dst_reg_ptr;
      vector_push(v, ir);
      return dst_reg_ptr;
    }
    case ND_GOTO:
    {
      IR *ir = calloc(1, sizeof(IR));
      ir->kind = IR_JMP;
      ir->jmp.label = node->jump.label_name;
      vector_push(v, ir);
      return NULL;
    }
    case ND_LABEL:
    {
      IR *ir = calloc(1, sizeof(IR));
      ir->kind = IR_LABEL;
      ir->label.name = node->jump.label_name;
      vector_push(v, ir);
      return gen_stmt(v, node->jump.statement_child);
    }
    case ND_PREDECREMENT:
    case ND_POSTDECREMENT:
    case ND_PREINCREMENT:
    case ND_POSTINCREMENT:
    {
      int *pre_reg = gen_stmt(v, node->lhs);
      int *post_reg = gen_reg();
      bool is_inc =
          node->kind == ND_PREINCREMENT || node->kind == ND_POSTINCREMENT;
      IR *ir = calloc(1, sizeof(IR));
      if (node->type->type == TYPE_BOOL)
      {
        ir->kind = IR_MOV;
        ir->mov.is_imm = true;
        if (is_inc)
          ir->mov.imm_val = 1;
        else
          ir->mov.imm_val = 0;
        ir->mov.dst_reg = *post_reg;
      }
      else
      {
        if (is_inc)
          ir->kind = IR_ADD;
        else
          ir->kind = IR_SUB;
        ir->bin_op.lhs_reg = *pre_reg;
        ir->bin_op.rhs_reg = *gen_stmt(v, new_node_num(node->num_val));
        ir->bin_op.lhs_size = size_of(node->lhs->type);
        ir->bin_op.rhs_size = 8;
        ir->bin_op.dst_reg = *post_reg;
      }
      vector_push(v, ir);
      int *lhs_addr_ptr = gen_addr(v, node->lhs);
      IR *assign = calloc(1, sizeof(IR));
      assign->kind = IR_STORE;
      assign->mem.reg = *post_reg;
      assign->mem.mem_reg = *lhs_addr_ptr;
      assign->mem.offset = 0;
      assign->mem.size = size_of(node->type);
      vector_push(v, assign);

      if (node->kind == ND_PREINCREMENT || node->kind == ND_PREDECREMENT)
        return post_reg;  // pre operators should return post value
      else
        return pre_reg;
    }
    case ND_ADD:
    case ND_SUB:
    case ND_MUL:
    case ND_DIV:
    case ND_IDIV:
    case ND_EQ:
    case ND_NEQ:
    case ND_LT:
    case ND_LTE:
    case ND_INCLUSIVE_OR:
    case ND_EXCLUSIVE_OR:
    case ND_AND:
    case ND_LEFT_SHIFT:
    case ND_RIGHT_SHIFT:
    {
      // Binary operations.
      int *lhs_ptr = gen_stmt(v, node->lhs);
      int *rhs_ptr = gen_stmt(v, node->rhs);
      IR *ir = calloc(1, sizeof(IR));
      switch (node->kind)
      {
        case ND_ADD: ir->kind = IR_ADD; break;
        case ND_SUB: ir->kind = IR_SUB; break;
        case ND_MUL: ir->kind = IR_MUL; break;
        case ND_DIV: ir->kind = IR_OP_DIV; break;
        case ND_IDIV: ir->kind = IR_OP_IDIV; break;
        case ND_EQ: ir->kind = IR_EQ; break;
        case ND_NEQ: ir->kind = IR_NEQ; break;
        case ND_LT: ir->kind = IR_LT; break;
        case ND_LTE: ir->kind = IR_LTE; break;
        case ND_INCLUSIVE_OR: ir->kind = IR_OR; break;
        case ND_EXCLUSIVE_OR: ir->kind = IR_XOR; break;
        case ND_AND: ir->kind = IR_AND; break;
        case ND_LEFT_SHIFT:
          ir->kind = node->lhs->type->is_signed ? IR_SAL : IR_SHL;
          break;
        case ND_RIGHT_SHIFT:
          ir->kind = node->lhs->type->is_signed ? IR_SAR : IR_SHR;
          break;
        default: unreachable();
      }
      if (!lhs_ptr || !rhs_ptr)
        error_at(node->token->str, node->token->len, "invalid operator");
      ir->bin_op.lhs_reg = *lhs_ptr;
      ir->bin_op.rhs_reg = *rhs_ptr;
      ir->bin_op.lhs_size = size_of(node->lhs->type);
      ir->bin_op.rhs_size = size_of(node->rhs->type);
      int *dst_reg_ptr = gen_reg();
      ir->bin_op.dst_reg = *dst_reg_ptr;
      vector_push(v, ir);
      return dst_reg_ptr;
    }
    case ND_LOGICAL_OR:
    {
      char *false_label = gen_label();
      char *end_label = gen_label();
      int *dst_reg_ptr = gen_reg();

      int *lhs_ptr = gen_stmt(v, node->lhs);
      IR *ir1 = calloc(1, sizeof(IR));
      ir1->kind = IR_JE;
      ir1->jmp.label = false_label;
      ir1->jmp.cond_reg = *lhs_ptr;
      vector_push(v, ir1);

      IR *ir2 = calloc(1, sizeof(IR));
      ir2->kind = IR_MOV;
      ir2->mov.is_imm = true;
      ir2->mov.imm_val = 1;
      ir2->mov.dst_reg = *dst_reg_ptr;
      vector_push(v, ir2);

      IR *ir3 = calloc(1, sizeof(IR));
      ir3->kind = IR_JMP;
      ir3->jmp.label = end_label;
      vector_push(v, ir3);

      IR *ir4 = calloc(1, sizeof(IR));
      ir4->kind = IR_LABEL;
      ir4->label.name = false_label;
      vector_push(v, ir4);

      int *rhs_ptr = gen_stmt(v, node->rhs);
      IR *ir5 = calloc(1, sizeof(IR));
      ir5->kind = IR_MOV;
      ir5->mov.is_imm = false;
      ir5->mov.src_reg = *rhs_ptr;
      ir5->mov.dst_reg = *dst_reg_ptr;
      vector_push(v, ir5);

      IR *ir6 = calloc(1, sizeof(IR));
      ir6->kind = IR_LABEL;
      ir6->label.name = end_label;
      vector_push(v, ir6);

      return dst_reg_ptr;
    }
    case ND_LOGICAL_AND:
    {
      char *false_label = gen_label();
      char *end_label = gen_label();
      int *dst_reg_ptr = gen_reg();

      int *lhs_ptr = gen_stmt(v, node->lhs);
      IR *ir1 = calloc(1, sizeof(IR));
      ir1->kind = IR_JE;
      ir1->jmp.label = false_label;
      ir1->jmp.cond_reg = *lhs_ptr;
      vector_push(v, ir1);

      int *rhs_ptr = gen_stmt(v, node->rhs);
      IR *ir2 = calloc(1, sizeof(IR));
      ir2->kind = IR_JE;
      ir2->jmp.label = false_label;
      ir2->jmp.cond_reg = *rhs_ptr;
      vector_push(v, ir2);

      IR *ir3 = calloc(1, sizeof(IR));
      ir3->kind = IR_MOV;
      ir3->mov.is_imm = true;
      ir3->mov.imm_val = 1;
      ir3->mov.dst_reg = *dst_reg_ptr;
      vector_push(v, ir3);

      IR *ir4 = calloc(1, sizeof(IR));
      ir4->kind = IR_JMP;
      ir4->jmp.label = end_label;
      vector_push(v, ir4);

      IR *ir5 = calloc(1, sizeof(IR));
      ir5->kind = IR_LABEL;
      ir5->label.name = false_label;
      vector_push(v, ir5);

      IR *ir6 = calloc(1, sizeof(IR));
      ir6->kind = IR_MOV;
      ir6->mov.is_imm = true;
      ir6->mov.imm_val = 0;
      ir6->mov.dst_reg = *dst_reg_ptr;
      vector_push(v, ir6);

      IR *ir7 = calloc(1, sizeof(IR));
      ir7->kind = IR_LABEL;
      ir7->label.name = end_label;
      vector_push(v, ir7);

      return dst_reg_ptr;
    }
    case ND_COMMA:
    {
      gen_stmt(v, node->lhs);
      return gen_stmt(v, node->rhs);
    }
    case ND_BUILTINFUNC:
    {
      IR *builtin = calloc(1, sizeof(IR));
      switch (node->func.builtin_func)
      {
        case FUNC_ASM:
        {
          if (vector_size(node->func.expr) != 1)
            unreachable();
          builtin->kind = IR_BUILTIN_ASM;
          Node *child = vector_pop(node->func.expr);
          builtin->builtin_asm.asm_str = child->token->str;
          builtin->builtin_asm.asm_len = child->token->len;
          break;
        }
        default: unreachable();
      }
      vector_push(v, builtin);
      return NULL;
    }
    default:
      error_exit("unexpected node kind: %d", node->kind);
      return 0;  // Unreachable
  }
}

static void dump_ir_fp(IRProgram *program, FILE *fp)
{
  // Dump global variables
  for (size_t i = 0; i < vector_size(program->global_vars); i++)
  {
    GlobalVar *gvar = vector_peek_at(program->global_vars, i + 1);
    fprintf(fp, "GVAR %.*s %zu", (int)gvar->var_name_len, gvar->var_name,
            gvar->var_size);
    switch (gvar->how2_init)
    {
      case init_zero: fprintf(fp, " ZERO\n"); break;
      case init_val: fprintf(fp, " VAL %lld\n", gvar->init_val); break;
      case init_pointer:
        fprintf(fp, " VAR %.*s\n", (int)gvar->assigned_var.var_name_len,
                gvar->assigned_var.var_name);
        break;
      case init_string: fprintf(fp, " STRING %s\n", gvar->literal_name); break;
      default: unreachable(); break;
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
      IR *ir = vector_peek_at(func->IR_Blocks, j + 1);
      // Print in text format according to the IR kind
      switch (ir->kind)
      {
        case IR_CALL:
          fprintf(fp, "  CALL r%d, %.*s, (", ir->call.dst_reg,
                  (int)ir->call.func_name_size, ir->call.func_name);
          for (size_t k = 0; k < vector_size(ir->call.args); k++)
          {
            int *reg = vector_peek_at(ir->call.args, k + 1);
            fprintf(fp, "r%d%s", *reg,
                    (k == vector_size(ir->call.args) - 1) ? "" : ", ");
          }
          fprintf(fp, ")\n");
          break;
        case IR_FUNC_PROLOGUE: fprintf(fp, "  PROLOGUE\n"); break;
        case IR_FUNC_EPILOGUE: fprintf(fp, "  EPILOGUE\n"); break;
        case IR_RET: fprintf(fp, "  RET r%d\n", ir->ret.src_reg); break;
        case IR_MOV:
          if (ir->mov.is_imm)
            fprintf(fp, "  MOV r%d, %lld\n", ir->mov.dst_reg, ir->mov.imm_val);
          else
            fprintf(fp, "  MOV r%d, r%d\n", ir->mov.dst_reg, ir->mov.src_reg);
          break;
        case IR_ADD:
          fprintf(fp, "  ADD %s r%d, r%d, r%d\n",
                  get_size_prefix(ir->bin_op.lhs_size), ir->bin_op.dst_reg,
                  ir->bin_op.lhs_reg, ir->bin_op.rhs_reg);
          break;
        case IR_SUB:
          fprintf(fp, "  SUB %s r%d, r%d, r%d\n",
                  get_size_prefix(ir->bin_op.lhs_size), ir->bin_op.dst_reg,
                  ir->bin_op.lhs_reg, ir->bin_op.rhs_reg);
          break;
        case IR_MUL:
          fprintf(fp, "  MUL %s r%d, r%d, r%d\n",
                  get_size_prefix(ir->bin_op.lhs_size), ir->bin_op.dst_reg,
                  ir->bin_op.lhs_reg, ir->bin_op.rhs_reg);
          break;
        case IR_OP_DIV:
          fprintf(fp, "  DIV %s r%d, r%d, r%d\n",
                  get_size_prefix(ir->bin_op.lhs_size), ir->bin_op.dst_reg,
                  ir->bin_op.lhs_reg, ir->bin_op.rhs_reg);
          break;
        case IR_OP_IDIV:
          fprintf(fp, "  IDIV %s r%d, r%d, r%d\n",
                  get_size_prefix(ir->bin_op.lhs_size), ir->bin_op.dst_reg,
                  ir->bin_op.lhs_reg, ir->bin_op.rhs_reg);
          break;
        case IR_EQ:
          fprintf(fp, "  EQ %s r%d, r%d, r%d\n",
                  get_size_prefix(ir->bin_op.lhs_size), ir->bin_op.dst_reg,
                  ir->bin_op.lhs_reg, ir->bin_op.rhs_reg);
          break;
        case IR_NEQ:
          fprintf(fp, "  NEQ %s r%d, r%d, r%d\n",
                  get_size_prefix(ir->bin_op.lhs_size), ir->bin_op.dst_reg,
                  ir->bin_op.lhs_reg, ir->bin_op.rhs_reg);
          break;
        case IR_LT:
          fprintf(fp, "  LT %s r%d, r%d, r%d\n",
                  get_size_prefix(ir->bin_op.lhs_size), ir->bin_op.dst_reg,
                  ir->bin_op.lhs_reg, ir->bin_op.rhs_reg);
          break;
        case IR_LTE:
          fprintf(fp, "  LTE %s r%d, r%d, r%d\n",
                  get_size_prefix(ir->bin_op.lhs_size), ir->bin_op.dst_reg,
                  ir->bin_op.lhs_reg, ir->bin_op.rhs_reg);
          break;
        case IR_OR:
          fprintf(fp, "  OR %s r%d, r%d, r%d\n",
                  get_size_prefix(ir->bin_op.lhs_size), ir->bin_op.dst_reg,
                  ir->bin_op.lhs_reg, ir->bin_op.rhs_reg);
          break;
        case IR_XOR:
          fprintf(fp, "  XOR %s r%d, r%d, r%d\n",
                  get_size_prefix(ir->bin_op.lhs_size), ir->bin_op.dst_reg,
                  ir->bin_op.lhs_reg, ir->bin_op.rhs_reg);
          break;
        case IR_AND:
          fprintf(fp, "  AND %s r%d, r%d, r%d\n",
                  get_size_prefix(ir->bin_op.lhs_size), ir->bin_op.dst_reg,
                  ir->bin_op.lhs_reg, ir->bin_op.rhs_reg);
          break;
        case IR_SHL:
          fprintf(fp, "  SHL %s r%d, r%d, r%d\n",
                  get_size_prefix(ir->bin_op.lhs_size), ir->bin_op.dst_reg,
                  ir->bin_op.lhs_reg, ir->bin_op.rhs_reg);
          break;
        case IR_SHR:
          fprintf(fp, "  SHR %s r%d, r%d, r%d\n",
                  get_size_prefix(ir->bin_op.lhs_size), ir->bin_op.dst_reg,
                  ir->bin_op.lhs_reg, ir->bin_op.rhs_reg);
          break;
        case IR_SAL:
          fprintf(fp, "  SAL %s r%d, r%d, r%d\n",
                  get_size_prefix(ir->bin_op.lhs_size), ir->bin_op.dst_reg,
                  ir->bin_op.lhs_reg, ir->bin_op.rhs_reg);
          break;
        case IR_SAR:
          fprintf(fp, "  SAR %s r%d, r%d, r%d\n",
                  get_size_prefix(ir->bin_op.lhs_size), ir->bin_op.dst_reg,
                  ir->bin_op.lhs_reg, ir->bin_op.rhs_reg);
          break;
        case IR_BIT_NOT:
          fprintf(fp, "  BNOT r%d, r%d\n", ir->un_op.dst_reg,
                  ir->un_op.src_reg);
          break;
        case IR_JMP: fprintf(fp, "  JMP %s\n", ir->jmp.label); break;
        case IR_JNE:
          fprintf(fp, "  JNE %s, r%d\n", ir->jmp.label, ir->jmp.cond_reg);
          break;
        case IR_JE:
          fprintf(fp, "  JE %s, r%d\n", ir->jmp.label, ir->jmp.cond_reg);
          break;
        case IR_LOAD:
          fprintf(fp, "  LOAD %s r%d, [r%d + %d]\n",
                  get_size_prefix(ir->mem.size), ir->mem.reg, ir->mem.mem_reg,
                  ir->mem.offset);
          break;
        case IR_STORE:
          fprintf(fp, "  STORE %s [r%d + %d], r%d\n",
                  get_size_prefix(ir->mem.size), ir->mem.mem_reg,
                  ir->mem.offset, ir->mem.reg);
          break;
        case IR_STORE_ARG:
          fprintf(fp, "  STORE_ARG %s r%d, %d\n",
                  get_size_prefix(ir->store_arg.size), ir->store_arg.dst_reg,
                  ir->store_arg.arg_index);
          break;
        case IR_LEA:
          if (ir->lea.is_local)
          {
            fprintf(fp, "  LEA r%d, LOCAL %zu\n", ir->lea.dst_reg,
                    ir->lea.var_offset);
          }
          else
          {
            if (ir->lea.is_static)
            {
              fprintf(fp, "  LEA r%d, STATIC %.*s\n", ir->lea.dst_reg,
                      (int)ir->lea.var_name_len, ir->lea.var_name);
            }
            else
            {
              fprintf(fp, "  LEA r%d, GLOBAL %.*s\n", ir->lea.dst_reg,
                      (int)ir->lea.var_name_len, ir->lea.var_name);
            }
          }
          break;
        case IR_LABEL: fprintf(fp, "%s:\n", ir->label.name); break;
        case IR_NEG:
          fprintf(fp, "  NEG r%d, r%d\n", ir->un_op.dst_reg, ir->un_op.src_reg);
          break;
        case IR_NOT:
          fprintf(fp, "  NOT r%d, r%d\n", ir->un_op.dst_reg, ir->un_op.src_reg);
          break;
        case IR_BUILTIN_ASM:
          fprintf(fp, "  ASM \"%.*s\"", (int)ir->builtin_asm.asm_len,
                  ir->builtin_asm.asm_str);
          break;
        default: unimplemented(); break;
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
