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
#include <assert.h>
#endif

#include "include/debug.h"
#include "include/error.h"
#include "include/parser.h"
#include "include/vector.h"

#ifdef SELF_HOST
#ifndef assert
#define assert(cond)                                                           \
  do                                                                           \
  {                                                                            \
    if (!(cond))                                                               \
      unreachable();                                                           \
  } while (0)
#endif
#endif

#if DEBUG
// Debug output macro
#define output_debug(fmt, ...) \
  printf("# %s:%d:%s() " fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__);
#else
#define output_debug(fmt, ...)
#endif

IR_Blocks *new_ir_blocks()
{
  IR_Blocks *new = malloc(sizeof(IR_Blocks));
  new->IRs = vector_new();
  new->parent = vector_new();
  new->lhs = NULL;
  new->rhs = NULL;
  new->reg_in = vector_new();
  new->reg_use = vector_new();
  new->reg_def = vector_new();
  new->reg_out = vector_new();
  return new;
}

// Virtual register ID
static size_t reg_id;
// Vector of IR_REG*
static Vector *virtual_regs;
// Generates a new virtual register and returns its ID.
static IR_REG *gen_reg(void)
{
  IR_REG *new_reg = calloc(1, sizeof(IR_REG));
  new_reg->reg_num = reg_id++;
  new_reg->used_list = vector_new();
  vector_push(virtual_regs, new_reg);
  return new_reg;
}

// Label ID
static size_t label_id;
// Generates a new label name (e.g., .L0, .L1, ...).
static char *gen_label()
{
  char buf[20];
  sprintf(buf, ".L%ld", label_id++);
  char *name = malloc(strlen(buf) + 1);
  strcpy(name, buf);
  return name;
}

OperandSize num2OpSize(size_t num)
{
  switch (num)
  {
    case 1: return SIZE_BYTE;
    case 2: return SIZE_WORD;
    case 4: return SIZE_DWORD;
    case 8: return SIZE_QWORD;
    default: unreachable(); return SIZE_RESERVED;
  }
}

static size_t legal_store_size(size_t remaining)
{
  if (remaining >= 8) return 8;
  if (remaining >= 4) return 4;
  if (remaining >= 2) return 2;
  return 1;
}

static bool is_aggregate_type(Type *type)
{
  return type && type->type == TYPE_STRUCT;
}

// Forward declarations
static IR_REG *gen_addr(Vector *blocks, Vector *labels, IR_Blocks **irs,
                        Node *node);
static IR_REG *gen_stmt(Vector *blocks, Vector *labels, IR_Blocks **irs,
                        Node *node);

static IR_REG *emit_imm(IR_Blocks **irs, long long value)
{
  IR *ir = calloc(1, sizeof(IR));
  ir->kind = IR_MOV;
  ir->mov.is_imm = true;
  ir->mov.imm_val = value;
  IR_REG *dst_reg_ptr = gen_reg();
  dst_reg_ptr->reg_size = SIZE_DWORD;  // int
  ir->mov.dst_reg = dst_reg_ptr;
  vector_push(dst_reg_ptr->used_list, ir);
  vector_push((*irs)->IRs, ir);
  return dst_reg_ptr;
}

static IR_REG *gen_incdec(Vector *blocks, Vector *labels, IR_Blocks **irs,
                          Node *node)
{
  IR_REG *pre_reg = gen_stmt(blocks, labels, irs, node->lhs);
  IR_REG *post_reg = gen_reg();
  post_reg->reg_size = num2OpSize(size_of_real(node->type->type));
  bool is_inc =
      node->kind == ND_PREINCREMENT || node->kind == ND_POSTINCREMENT;
  IR *ir = calloc(1, sizeof(IR));
  if (node->type->type == TYPE_BOOL)
  {
    ir->kind = IR_MOV;
    ir->mov.is_imm = true;
    ir->mov.imm_val = is_inc ? 1 : 0;
    ir->mov.dst_reg = post_reg;
    vector_push(post_reg->used_list, ir);
  }
  else
  {
    IR_REG *rhs_reg = emit_imm(irs, node->num_val);
    ir->kind = is_inc ? IR_ADD : IR_SUB;
    ir->bin_op.lhs_reg = pre_reg;
    vector_push(pre_reg->used_list, ir);
    ir->bin_op.rhs_reg = rhs_reg;
    vector_push(rhs_reg->used_list, ir);
    ir->bin_op.dst_reg = post_reg;
    vector_push(post_reg->used_list, ir);
  }
  vector_push((*irs)->IRs, ir);

  IR_REG *lhs_addr_ptr = gen_addr(blocks, labels, irs, node->lhs);
  IR *assign = calloc(1, sizeof(IR));
  assign->kind = IR_STORE;
  assign->mem.reg = post_reg;
  vector_push(post_reg->used_list, assign);
  assign->mem.mem_reg = lhs_addr_ptr;
  assign->mem.offset = 0;
  assign->mem.size = num2OpSize(size_of_real(node->type->type));
  vector_push((*irs)->IRs, assign);

  if (node->kind == ND_PREINCREMENT || node->kind == ND_PREDECREMENT)
    return post_reg;
  return pre_reg;
}

IRFunc *call_builtin_func(Node *node)
{
  IRFunc *func = calloc(1, sizeof(IRFunc));
  func->builtin_func = FUNC_ASM;
  func->IR_Blocks = vector_new();
  IR_Blocks *irs = new_ir_blocks();
  vector_push(func->IR_Blocks, irs);
  gen_stmt(func->IR_Blocks, func->labels, &irs, node);
  if (vector_size(func->IR_Blocks) != 1 || vector_size(irs->IRs) != 1)
    unreachable();
  return func;
}

void gen_gvar_assign(IR_Blocks *irs, Type *assigned, Node *node,
                     size_t assign_size)
{
  if (node->kind == ND_ASSIGN)
  {
    gen_gvar_assign(irs, node->lhs->type, node->rhs, size_of(node->type));
    return;
  }

  if (node->kind == ND_INITIALIZER)
  {
    for (size_t i = 0; i < vector_size(node->initialize.init_list); i++)
    {
      Node *child = vector_peek_at(node->initialize.init_list, i + 1);
      gen_gvar_assign(irs, assigned->ptr_to, child, size_of(child->type));
    }
    size_t zero_padding =
        size_of(node->type) -
        vector_size(node->initialize.init_list) * size_of(node->type->ptr_to);
    if (zero_padding)
    {
      GVarInitializer *zero = calloc(1, sizeof(GVarInitializer));
      zero->how2_init = init_zero;
      zero->zero_len = zero_padding;
      vector_push(irs->IRs, zero);
    }
    return;
  }

  GVarInitializer *init = calloc(1, sizeof(GVarInitializer));
  switch (node->kind)
  {
    case ND_NUM:
      init->how2_init = init_val;
      init->value.init_val = node->num_val;
      init->value.value_size = assign_size;
      break;
    case ND_ADDR:
      if (assign_size != 8)
        unreachable();
      init->how2_init = init_pointer;
      init->assigned_var.var_name = node->lhs->variable.var->name;
      init->assigned_var.var_name_len = node->lhs->variable.var->len;
      break;
    case ND_STRING:
      if (assign_size != 8)
        unreachable();
      init->how2_init = init_string;
      init->literal_name = node->literal_name;
      break;
    default:
      unreachable();
      break;
  }
  vector_push(irs->IRs, init);
}

static void append_global_var(IRProgram *program, Node *node)
{
  if (node->variable.var->storage_class_specifier & 1 << 1)
    return;

  GlobalVar *gvar = calloc(1, sizeof(GlobalVar));
  gvar->var_name = node->variable.var->name;
  gvar->var_name_len = node->variable.var->len;
  gvar->var_size = size_of(node->variable.var->type);
  gvar->is_static = node->variable.var->storage_class_specifier & 1 << 2;
  GVarInitializer *init = calloc(1, sizeof(GVarInitializer));
  init->how2_init = init_zero;
  init->zero_len = size_of(node->variable.var->type);
  IR_Blocks *irs = new_ir_blocks();
  vector_push(irs->IRs, init);
  gvar->initializer = irs;
  vector_push(program->global_vars, gvar);
}

static bool can_emit_flat_global_initializer(Type *type, Node *node)
{
  if (node->kind == ND_ASSIGN)
    node = node->rhs;
  if (type->type == TYPE_ARRAY)
  {
    if (node->kind != ND_INITIALIZER)
      return false;
    for (size_t i = 0; i < vector_size(node->initialize.init_list); i++)
    {
      Node *child = vector_peek_at(node->initialize.init_list, i + 1);
      if (!can_emit_flat_global_initializer(type->ptr_to, child))
        return false;
    }
    return true;
  }

  return node->kind == ND_NUM || node->kind == ND_ADDR ||
         node->kind == ND_STRING;
}

static void emit_flat_global_initializer(IR_Blocks *irs, Type *type, Node *node)
{
  if (node->kind == ND_ASSIGN)
    node = node->rhs;

  if (type->type == TYPE_ARRAY)
  {
    size_t init_len = vector_size(node->initialize.init_list);
    for (size_t i = 0; i < init_len; i++)
    {
      Node *child = vector_peek_at(node->initialize.init_list, i + 1);
      emit_flat_global_initializer(irs, type->ptr_to, child);
    }

    size_t zero_padding = size_of(type) - init_len * size_of(type->ptr_to);
    if (zero_padding)
    {
      GVarInitializer *zero = calloc(1, sizeof(GVarInitializer));
      zero->how2_init = init_zero;
      zero->zero_len = zero_padding;
      vector_push(irs->IRs, zero);
    }
    return;
  }

  GVarInitializer *init = calloc(1, sizeof(GVarInitializer));
  if (node->kind == ND_NUM)
  {
    init->how2_init = init_val;
    init->value.init_val = node->num_val;
    init->value.value_size = size_of(type);
  }
  else if (node->kind == ND_ADDR)
  {
    if (size_of(type) != 8)
      unreachable();
    init->how2_init = init_pointer;
    init->assigned_var.var_name = node->lhs->variable.var->name;
    init->assigned_var.var_name_len = node->lhs->variable.var->len;
  }
  else
  {
    if (size_of(type) != 8)
      unreachable();
    init->how2_init = init_string;
    init->literal_name = node->literal_name;
  }
  vector_push(irs->IRs, init);
}

static void append_global_assign(IRProgram *program, Node *node)
{
  Node *lhs = node->lhs;
  Node *rhs = node->rhs;
  GlobalVar *gvar = calloc(1, sizeof(GlobalVar));
  gvar->var_name = lhs->variable.var->name;
  gvar->var_name_len = lhs->variable.var->len;
  gvar->var_size = size_of(lhs->variable.var->type);
  gvar->is_static = lhs->variable.var->storage_class_specifier & 1 << 2;
  IR_Blocks *irs = new_ir_blocks();
  gvar->initializer = irs;

  if (rhs->kind == ND_NUM)
  {
    GVarInitializer *init = calloc(1, sizeof(GVarInitializer));
    init->how2_init = init_val;
    init->value.init_val = rhs->num_val;
    init->value.value_size = size_of(node->type);
    vector_push(irs->IRs, init);
  }
  else if (rhs->kind == ND_ADDR)
  {
    if (size_of(node->type) != 8)
      unreachable();
    GVarInitializer *init = calloc(1, sizeof(GVarInitializer));
    init->how2_init = init_pointer;
    init->assigned_var.var_name = rhs->lhs->variable.var->name;
    init->assigned_var.var_name_len = rhs->lhs->variable.var->len;
    vector_push(irs->IRs, init);
  }
  else if (rhs->kind == ND_STRING)
  {
    if (lhs->type->type == TYPE_ARRAY)
    {
      GVarInitializer *init = calloc(1, sizeof(GVarInitializer));
      init->how2_init = init_string_data;
      init->literal_name = rhs->literal_name;
      vector_push(irs->IRs, init);

      size_t zero_padding = size_of(lhs->type) - (rhs->token->len + 1);
      if (zero_padding)
      {
        GVarInitializer *zero = calloc(1, sizeof(GVarInitializer));
        zero->how2_init = init_zero;
        zero->zero_len = zero_padding;
        vector_push(irs->IRs, zero);
      }
    }
    else
    {
      if (size_of(node->type) != 8)
        unreachable();
      GVarInitializer *init = calloc(1, sizeof(GVarInitializer));
      init->how2_init = init_string;
      init->literal_name = rhs->literal_name;
      vector_push(irs->IRs, init);
    }
  }
  else if (can_emit_flat_global_initializer(lhs->type, rhs))
  {
    emit_flat_global_initializer(irs, lhs->type, rhs);
  }
  else
  {
    gen_gvar_assign(irs, lhs->type, node, size_of(node->type));
  }
  vector_push(program->global_vars, gvar);
}

IRProgram *gen_ir(FuncBlock *parsed)
{
  IRProgram *program = calloc(1, sizeof(IRProgram));
  program->functions = vector_new();
  program->global_vars = vector_new();

  // Loop through all functions/global variables
  for (FuncBlock *fb = parsed; fb; fb = fb->next)
  {
    Node *node = fb->node;
    if (node)
    {
      switch (node->kind)
      {
        case ND_NOP: break;
        case ND_FUNCDEF:
        {
          // Case: Function definition
          IRFunc *func = calloc(1, sizeof(IRFunc));
          func->user_defined.function_name = node->token->str;
          func->user_defined.function_name_size = node->token->len;
          func->user_defined.is_static =
              node->func.storage_class_specifier & 1 << 2;
          func->IR_Blocks = vector_new();
          func->labels = vector_new();
          IR_Blocks *irs = new_ir_blocks();
          vector_push(func->IR_Blocks, irs);

          reg_id = 0;  // Reset register ID for each function
          virtual_regs = func->user_defined.num_virtual_regs = vector_new();
          gen_stmt(func->IR_Blocks, func->labels, &irs,
                   node);  // Generate IR for statements

          func->user_defined.stack_size = fb->stacksize;
          vector_push(program->functions, func);
          break;
        }
        case ND_BUILTINFUNC:
        {
          vector_push(program->functions, call_builtin_func(node));
          break;
        }
        case ND_VAR:
        {
          append_global_var(program, node);
          break;
        }
        case ND_ASSIGN:
        {
          append_global_assign(program, node);
          break;
        }
        default: unreachable(); break;
      }
    }
  }

  program->strings = get_string_list();

#if DEBUG
  // If it's a debug build, dump the generated IR to stdout.
  dump_ir_fp(program, stdout);
#endif

  return program;
}

typedef struct
{
  Vector *blocks;
  Vector *labels;
  IR_Blocks **irs;
  Node *assigned;
  size_t padding;
} AssignContext;

static IR_REG *gen_assign(AssignContext *ctx, Node *node, size_t assign_size)
{
  if (node->kind == ND_INITIALIZER)
  {
    for (size_t i = 0; i < vector_size(node->initialize.init_list); i++)
    {
      Node *child = vector_peek_at(node->initialize.init_list, i + 1);
      AssignContext child_ctx;
      child_ctx.blocks = ctx->blocks;
      child_ctx.labels = ctx->labels;
      child_ctx.irs = ctx->irs;
      child_ctx.assigned = ctx->assigned;
      child_ctx.padding = ctx->padding + i * size_of(node->type->ptr_to);
      gen_assign(&child_ctx, child, size_of(child->type));
    }
    // Uninitialized array elements are padded with zeros
    size_t done_init =
        vector_size(node->initialize.init_list) * size_of(node->type->ptr_to);
    while (size_of(node->type) - done_init)
    {
      size_t size = legal_store_size(size_of(node->type) - done_init);
      IR_REG *zero_reg = emit_imm(ctx->irs, 0);
      IR_REG *lhs_addr_ptr =
          gen_addr(ctx->blocks, ctx->labels, ctx->irs, ctx->assigned);
      IR *ir = calloc(1, sizeof(IR));
      ir->kind = IR_STORE;
      ir->mem.reg = zero_reg;
      vector_push(zero_reg->used_list, ir);
      ir->mem.mem_reg = lhs_addr_ptr;
      vector_push(lhs_addr_ptr->used_list, ir);
      ir->mem.offset = ctx->padding + done_init;
      ir->mem.size = num2OpSize(size);
      vector_push((*ctx->irs)->IRs, ir);
      done_init += size;
    }
    return NULL;
  }
  else if (is_aggregate_type(node->type))
  {
    IR_REG *rhs_addr_ptr = gen_addr(ctx->blocks, ctx->labels, ctx->irs, node);
    IR_REG *lhs_addr_ptr =
        gen_addr(ctx->blocks, ctx->labels, ctx->irs, ctx->assigned);
    size_t copy_size = size_of(node->type);
    for (size_t done = 0; done < copy_size;)
    {
      size_t chunk = legal_store_size(copy_size - done);
      OperandSize op_size = num2OpSize(chunk);

      IR_REG *tmp_reg = gen_reg();
      tmp_reg->reg_size = op_size;

      IR *load = calloc(1, sizeof(IR));
      load->kind = IR_LOAD;
      load->mem.reg = tmp_reg;
      vector_push(tmp_reg->used_list, load);
      load->mem.mem_reg = rhs_addr_ptr;
      vector_push(rhs_addr_ptr->used_list, load);
      load->mem.offset = ctx->padding + done;
      load->mem.size = op_size;
      vector_push((*ctx->irs)->IRs, load);

      IR *store = calloc(1, sizeof(IR));
      store->kind = IR_STORE;
      store->mem.reg = tmp_reg;
      vector_push(tmp_reg->used_list, store);
      store->mem.mem_reg = lhs_addr_ptr;
      vector_push(lhs_addr_ptr->used_list, store);
      store->mem.offset = ctx->padding + done;
      store->mem.size = op_size;
      vector_push((*ctx->irs)->IRs, store);

      done += chunk;
    }
    return NULL;
  }
  else
  {
    // For an assignment, evaluate the RHS, then STORE it at the address of
    // the LHS.
    IR_REG *rhs_ptr = gen_stmt(ctx->blocks, ctx->labels, ctx->irs, node);
    IR_REG *lhs_addr_ptr =
        gen_addr(ctx->blocks, ctx->labels, ctx->irs, ctx->assigned);
    IR *ir = calloc(1, sizeof(IR));
    ir->kind = IR_STORE;
    ir->mem.reg = rhs_ptr;
    vector_push(rhs_ptr->used_list, ir);
    ir->mem.mem_reg = lhs_addr_ptr;
    vector_push(lhs_addr_ptr->used_list, ir);
    ir->mem.offset = ctx->padding;
    ir->mem.size = num2OpSize(assign_size);
    vector_push((*ctx->irs)->IRs, ir);
    return rhs_ptr;  // The result of an assignment expression is the RHS
                     // value.
  }
}

static IR_REG *gen_addr(Vector *blocks, Vector *labels, IR_Blocks **irs,
                        Node *node)
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
        ir->lea.var_offset = -node->variable.var->offset;
      IR_REG *dst_reg_ptr = gen_reg();
      dst_reg_ptr->reg_size = SIZE_QWORD;  // ptr
      vector_push(dst_reg_ptr->used_list, ir);
      ir->lea.dst_reg = dst_reg_ptr;
      output_debug("gen_addr: LEA var name = %.*s", (int)ir->lea.var_name_len,
                   ir->lea.var_name);
      vector_push((*irs)->IRs, ir);
      return dst_reg_ptr;
    }
    case ND_DEREF:
    {
      // For a dereference, evaluate the expression on the left of `*` to get
      // the address.
      return gen_stmt(blocks, labels, irs, node->lhs);
    }
    case ND_DOT:
    case ND_ARROW:
    {
      IR_REG *lhs_addr = node->kind == ND_DOT
                             ? gen_addr(blocks, labels, irs, node->lhs)
                             : gen_stmt(blocks, labels, irs, node->lhs);
      IR_REG *offset_val = emit_imm(irs, node->child_offset);

      IR *ir = calloc(1, sizeof(IR));
      ir->kind = IR_ADD;
      ir->bin_op.lhs_reg = lhs_addr;
      vector_push(lhs_addr->used_list, ir);
      ir->bin_op.rhs_reg = offset_val;
      vector_push(offset_val->used_list, ir);
      IR_REG *dst_reg = gen_reg();
      dst_reg->reg_size = SIZE_QWORD;  // ptr
      ir->bin_op.dst_reg = dst_reg;
      vector_push(dst_reg->used_list, ir);
      vector_push((*irs)->IRs, ir);

      return dst_reg;
    }
    default: error_exit("not an lvalue"); return NULL;  // Unreachable
  }
}

static IR_REG *gen_stmt(Vector *blocks, Vector *labels, IR_Blocks **irs,
                        Node *node)
{
  if (!node || node->kind == ND_NOP)
    return NULL;
  switch (node->kind)
  {
    case ND_NUM:
    {
      return emit_imm(irs, node->num_val);
    }
    case ND_DOT:
    case ND_ARROW:
    case ND_VAR:
    {
      // If it's an array type, return its address.
      if (node->type->type == TYPE_ARRAY)
      {
        return gen_addr(blocks, labels, irs, node);
      }
      // For a variable, get its address and then generate a LOAD instruction.
      IR_REG *addr_reg_ptr = gen_addr(blocks, labels, irs, node);
      IR *ir = calloc(1, sizeof(IR));
      ir->kind = IR_LOAD;
      ir->mem.mem_reg = addr_reg_ptr;
      vector_push(addr_reg_ptr->used_list, ir);
      ir->mem.offset = 0;
      IR_REG *reg_ptr = gen_reg();
      reg_ptr->reg_size = num2OpSize(size_of_real(node->type->type));
      ir->mem.reg = reg_ptr;
      vector_push(reg_ptr->used_list, ir);
      ir->mem.size = num2OpSize(size_of_real(node->type->type));
      vector_push((*irs)->IRs, ir);
      return reg_ptr;
    }
    case ND_STRING:
    {
      // For a string literal, add it to the global string list and then
      // generate a LEA instruction to get its address.
      char* literal_name = node->literal_name;
      if (!literal_name)
        literal_name = "(null)";
      pr_debug("IR: handling ND_STRING literal %s",
               literal_name);
      IR *ir = calloc(1, sizeof(IR));
      ir->kind = IR_LEA;
      ir->lea.is_local = false;
      ir->lea.is_static = true;
      ir->lea.var_name = node->literal_name;
      if (!node->literal_name)
        unreachable();
      ir->lea.var_name_len = strlen(node->literal_name);
      IR_REG *dst_reg_ptr = gen_reg();
      // On x86_64, pointers are 64-bit
      dst_reg_ptr->reg_size = SIZE_QWORD;
      ir->lea.dst_reg = dst_reg_ptr;
      vector_push(dst_reg_ptr->used_list, ir);
      vector_push((*irs)->IRs, ir);
      return dst_reg_ptr;
    }
    case ND_ASSIGN:
    {
      Type *assign_type = node->type;
      AssignContext ctx;
      ctx.blocks = blocks;
      ctx.labels = labels;
      ctx.irs = irs;
      ctx.assigned = node->lhs;
      ctx.padding = 0;
      pr_debug("IR: ND_ASSIGN");
      size_t assign_size = is_aggregate_type(assign_type)
                               ? size_of(assign_type)
                               : size_of_real(assign_type->type);
      return gen_assign(&ctx, node->rhs, assign_size);
    }
    case ND_RETURN:
    {
      pr_debug("IR: ND_RETURN enter");
      // For a return statement, evaluate the return value and generate a RET
      // instruction.
      IR_REG *src_ptr = NULL;
      if (node->lhs)
        src_ptr = gen_stmt(blocks, labels, irs, node->lhs);
      IR *ir = calloc(1, sizeof(IR));
      ir->kind = IR_RET;
      if (src_ptr)
      {
        ir->ret.src_reg = src_ptr;
        vector_push(src_ptr->used_list, ir);
        ir->ret.return_void = false;
      }
      else
        ir->ret.return_void = true;
      vector_push((*irs)->IRs, ir);
      pr_debug("IR: ND_RETURN queued");
      *irs = new_ir_blocks();
      vector_push(blocks, *irs);
      return NULL;  // A return statement does not produce a value.
    }
    case ND_IF:
    case ND_ELIF:
    {
      char *else_label = gen_label();
      char *end_label = gen_label();

      // Evaluate the condition.
      IR_REG *cond_reg_ptr =
          gen_stmt(blocks, labels, irs, node->control.condition);
      // If the condition is false (0), jump to else_label.
      IR *ir = calloc(1, sizeof(IR));
      ir->kind = IR_JE;  // Jump if Equal (to zero)
      ir->jmp.label = else_label;
      ir->jmp.cond_reg = cond_reg_ptr;
      vector_push(cond_reg_ptr->used_list, ir);
      vector_push((*irs)->IRs, ir);
      *irs = new_ir_blocks();
      vector_push(blocks, *irs);

      // Generate code for the 'true' block.
      gen_stmt(blocks, labels, irs, node->control.true_code);
      // Jump to the end_label.
      IR *jmp_ir = calloc(1, sizeof(IR));
      jmp_ir->kind = IR_JMP;
      jmp_ir->jmp.label = end_label;
      vector_push((*irs)->IRs, jmp_ir);
      *irs = new_ir_blocks();
      vector_push(blocks, *irs);

      // Place the 'else' label.
      if (vector_size((*irs)->IRs))
      {
        vector_push(blocks, *irs = new_ir_blocks());
      }
      IR *else_label_ir = calloc(1, sizeof(IR));
      else_label_ir->kind = IR_LABEL;
      else_label_ir->label.name = else_label;
      vector_push((*irs)->IRs, else_label_ir);
      assert(vector_size((*irs)->IRs) == 1);
      vector_push(labels, *irs);

      // If there is a 'false' block (else, else if), generate its code.
      if (node->control.false_code)
      {
        gen_stmt(blocks, labels, irs, node->control.false_code);
      }

      // Place the 'end' label.
      if (vector_size((*irs)->IRs))
      {
        vector_push(blocks, *irs = new_ir_blocks());
      }
      IR *end_label_ir = calloc(1, sizeof(IR));
      end_label_ir->kind = IR_LABEL;
      end_label_ir->label.name = end_label;
      vector_push((*irs)->IRs, end_label_ir);
      assert(vector_size((*irs)->IRs) == 1);
      vector_push(labels, *irs);
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
      if (vector_size((*irs)->IRs))
      {
        vector_push(blocks, *irs = new_ir_blocks());
      }
      IR *begin_label_ir = calloc(1, sizeof(IR));
      begin_label_ir->kind = IR_LABEL;
      begin_label_ir->label.name = begin_label;
      vector_push((*irs)->IRs, begin_label_ir);
      assert(vector_size((*irs)->IRs) == 1);
      vector_push(labels, *irs);

      // Evaluate the condition.
      IR_REG *cond_reg_ptr =
          gen_stmt(blocks, labels, irs, node->control.condition);
      // If the condition is false (0), jump to end_label.
      IR *ir = calloc(1, sizeof(IR));
      ir->kind = IR_JE;
      ir->jmp.label = end_label;
      ir->jmp.cond_reg = cond_reg_ptr;
      vector_push(cond_reg_ptr->used_list, ir);
      vector_push((*irs)->IRs, ir);
      *irs = new_ir_blocks();
      vector_push(blocks, *irs);

      // Generate code for the loop body.
      gen_stmt(blocks, labels, irs, node->control.true_code);
      // Jump to the loop start label.
      IR *jmp_ir = calloc(1, sizeof(IR));
      jmp_ir->kind = IR_JMP;
      jmp_ir->jmp.label = begin_label;
      vector_push((*irs)->IRs, jmp_ir);
      *irs = new_ir_blocks();
      vector_push(blocks, *irs);

      // Place the loop end label.
      if (vector_size((*irs)->IRs))
      {
        vector_push(blocks, *irs = new_ir_blocks());
      }
      IR *end_label_ir = calloc(1, sizeof(IR));
      end_label_ir->kind = IR_LABEL;
      end_label_ir->label.name = end_label;
      vector_push((*irs)->IRs, end_label_ir);
      assert(vector_size((*irs)->IRs) == 1);
      vector_push(labels, *irs);
      return NULL;
    }
    case ND_DO:
    {
      char *label_name = malloc(10 + node->control.label->len);
      snprintf(label_name, 10 + node->control.label->len, ".Lbegindo%.*s",
               (int)node->control.label->len, node->control.label->name);
      if (vector_size((*irs)->IRs))
      {
        vector_push(blocks, *irs = new_ir_blocks());
      }
      IR *label = calloc(1, sizeof(IR));
      label->kind = IR_LABEL;
      label->label.name = label_name;
      vector_push((*irs)->IRs, label);
      assert(vector_size((*irs)->IRs) == 1);
      vector_push(labels, *irs);
      gen_stmt(blocks, labels, irs, node->control.true_code);
      IR_REG *cond_ptr = gen_stmt(blocks, labels, irs, node->control.condition);
      IR *condition = calloc(1, sizeof(IR));
      condition->kind = IR_JNE;
      condition->jmp.label = label_name;
      condition->jmp.cond_reg = cond_ptr;
      vector_push(cond_ptr->used_list, condition);
      vector_push((*irs)->IRs, condition);
      *irs = new_ir_blocks();
      vector_push(blocks, *irs);
      char *end_name = malloc(8 + node->control.label->len);
      snprintf(end_name, 8 + node->control.label->len, ".Lenddo%.*s",
               (int)node->control.label->len, node->control.label->name);
      if (vector_size((*irs)->IRs))
      {
        vector_push(blocks, *irs = new_ir_blocks());
      }
      IR *end_label = calloc(1, sizeof(IR));
      end_label->kind = IR_LABEL;
      end_label->label.name = end_name;
      vector_push((*irs)->IRs, end_label);
      assert(vector_size((*irs)->IRs) == 1);
      vector_push(labels, *irs);
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
        gen_stmt(blocks, labels, irs, node->control.init);

      // Place the loop start label.
      if (vector_size((*irs)->IRs))
      {
        vector_push(blocks, *irs = new_ir_blocks());
      }
      IR *begin_label_ir = calloc(1, sizeof(IR));
      begin_label_ir->kind = IR_LABEL;
      begin_label_ir->label.name = begin_label;
      vector_push((*irs)->IRs, begin_label_ir);
      assert(vector_size((*irs)->IRs) == 1);
      vector_push(labels, *irs);

      // If there is a condition, evaluate it and exit the loop if false.
      if (node->control.condition)
      {
        IR_REG *cond_reg_ptr =
            gen_stmt(blocks, labels, irs, node->control.condition);
        IR *ir = calloc(1, sizeof(IR));
        ir->kind = IR_JE;
        ir->jmp.label = end_label;
        ir->jmp.cond_reg = cond_reg_ptr;
        vector_push(cond_reg_ptr->used_list, ir);
        vector_push((*irs)->IRs, ir);
        *irs = new_ir_blocks();
        vector_push(blocks, *irs);
      }

      // Generate code for the loop body.
      gen_stmt(blocks, labels, irs, node->control.true_code);
      // Generate the update expression if it exists.
      if (node->control.update)
        gen_stmt(blocks, labels, irs, node->control.update);

      // Jump to the loop start label.
      IR *jmp_ir = calloc(1, sizeof(IR));
      jmp_ir->kind = IR_JMP;
      jmp_ir->jmp.label = begin_label;
      vector_push((*irs)->IRs, jmp_ir);
      *irs = new_ir_blocks();
      vector_push(blocks, *irs);

      // Place the loop end label.
      if (vector_size((*irs)->IRs))
      {
        vector_push(blocks, *irs = new_ir_blocks());
      }
      IR *end_label_ir = calloc(1, sizeof(IR));
      end_label_ir->kind = IR_LABEL;
      end_label_ir->label.name = end_label;
      vector_push((*irs)->IRs, end_label_ir);
      assert(vector_size((*irs)->IRs) == 1);
      vector_push(labels, *irs);
      return NULL;
    }
    case ND_TERNARY:
    {
      char *false_label = gen_label();
      char *end_label = gen_label();
      IR_REG *dst_reg_ptr1 = gen_reg();
      IR_REG *dst_reg_ptr2 = gen_reg();
      IR_REG *dst_reg_ptr = gen_reg();
      dst_reg_ptr->reg_size = dst_reg_ptr1->reg_size = dst_reg_ptr2->reg_size =
          num2OpSize(size_of_real(node->type->type));

      IR_REG *condition = gen_stmt(blocks, labels, irs, node->lhs);
      IR *jump = calloc(1, sizeof(IR));
      jump->kind = IR_JE;
      jump->jmp.label = false_label;
      jump->jmp.cond_reg = condition;
      vector_push(condition->used_list, jump);
      vector_push((*irs)->IRs, jump);
      *irs = new_ir_blocks();
      vector_push(blocks, *irs);

      IR_REG *true_reg =
          gen_stmt(blocks, labels, irs, node->control.ternary_child);
      IR *mov_true = calloc(1, sizeof(IR));
      mov_true->kind = IR_MOV;
      mov_true->mov.is_imm = false;
      mov_true->mov.src_reg = true_reg;
      vector_push(true_reg->used_list, mov_true);
      mov_true->mov.dst_reg = dst_reg_ptr1;
      vector_push(dst_reg_ptr1->used_list, mov_true);
      vector_push((*irs)->IRs, mov_true);
      IR *jump_end = calloc(1, sizeof(IR));
      jump_end->kind = IR_JMP;
      jump_end->jmp.label = end_label;
      vector_push((*irs)->IRs, jump_end);
      *irs = new_ir_blocks();
      vector_push(blocks, *irs);
      if (vector_size((*irs)->IRs))
      {
        vector_push(blocks, *irs = new_ir_blocks());
      }
      IR *label_false = calloc(1, sizeof(IR));
      label_false->kind = IR_LABEL;
      label_false->label.name = false_label;
      vector_push((*irs)->IRs, label_false);
      assert(vector_size((*irs)->IRs) == 1);
      vector_push(labels, *irs);
      IR_REG *false_reg = gen_stmt(blocks, labels, irs, node->rhs);
      IR *mov_false = calloc(1, sizeof(IR));
      mov_false->kind = IR_MOV;
      mov_false->mov.is_imm = false;
      mov_false->mov.src_reg = false_reg;
      vector_push(false_reg->used_list, mov_false);
      mov_false->mov.dst_reg = dst_reg_ptr2;
      vector_push(dst_reg_ptr2->used_list, mov_false);
      vector_push((*irs)->IRs, mov_false);
      if (vector_size((*irs)->IRs))
      {
        vector_push(blocks, *irs = new_ir_blocks());
      }
      IR *label_end = calloc(1, sizeof(IR));
      label_end->kind = IR_LABEL;
      label_end->label.name = end_label;
      vector_push((*irs)->IRs, label_end);
      assert(vector_size((*irs)->IRs) == 1);
      vector_push(labels, *irs);

      // phi instruction
      IR *phi = calloc(1, sizeof(IR));
      phi->kind = IR_PHI;
      phi->phi.dst_reg = dst_reg_ptr;
      phi->phi.lhs_reg = dst_reg_ptr1;
      phi->phi.rhs_reg = dst_reg_ptr2;
      vector_push(dst_reg_ptr1->used_list, phi);
      vector_push(dst_reg_ptr2->used_list, phi);
      vector_push(dst_reg_ptr->used_list, phi);
      vector_push((*irs)->IRs, phi);
      return dst_reg_ptr;
    }
    case ND_CASE:
    {
      char *label_name = malloc(14 + node->jump.switch_name->len);
      snprintf(label_name, 14 + node->jump.switch_name->len, ".Lswitch%.*s_%lu",
               (int)node->jump.switch_name->len, node->jump.switch_name->name,
               node->jump.case_num);
      if (vector_size((*irs)->IRs))
      {
        vector_push(blocks, *irs = new_ir_blocks());
      }
      if (vector_size((*irs)->IRs))
      {
        vector_push(blocks, *irs = new_ir_blocks());
      }
      IR *label = calloc(1, sizeof(IR));
      label->kind = IR_LABEL;
      label->label.name = label_name;
      vector_push((*irs)->IRs, label);
      assert(vector_size((*irs)->IRs) == 1);
      vector_push(labels, *irs);
      return gen_stmt(blocks, labels, irs, node->jump.statement_child);
    }
    case ND_SWITCH:
    {
      IR_REG *cond_ptr = gen_stmt(blocks, labels, irs, node->control.condition);
      GTLabel *switch_label = node->control.label;
      IR *default_jmp = NULL;
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
          cmp->bin_op.lhs_reg = cond_ptr;
          vector_push(cond_ptr->used_list, cmp);
        IR_REG *rhs_reg = emit_imm(irs, case_child->jump.constant_expression);
          cmp->bin_op.rhs_reg = rhs_reg;
          vector_push(rhs_reg->used_list, cmp);
          IR_REG *cmp_result = gen_reg();
          cmp_result->reg_size = SIZE_DWORD;  // int
          cmp->bin_op.dst_reg = cmp_result;
          vector_push(cmp_result->used_list, cmp);
          vector_push((*irs)->IRs, cmp);
          IR *jump = calloc(1, sizeof(IR));
          jump->kind = IR_JNE;
          jump->jmp.label = label_name;
          jump->jmp.cond_reg = cmp_result;
          vector_push(cmp_result->used_list, jump);
          vector_push((*irs)->IRs, jump);
          *irs = new_ir_blocks();
          vector_push(blocks, *irs);
        }
        else
        {  // default
          IR *jump = calloc(1, sizeof(IR));
          jump->kind = IR_JMP;
          jump->jmp.label = label_name;
          default_jmp = jump;
        }
      }
      if (default_jmp)
      {
        vector_push((*irs)->IRs, default_jmp);
        *irs = new_ir_blocks();
        vector_push(blocks, *irs);
      }
      char *label_name = malloc(12 + switch_label->len);
      snprintf(label_name, 12 + switch_label->len, ".Lendswitch%.*s",
               (int)switch_label->len, switch_label->name);
      IR *jump = calloc(1, sizeof(IR));
      jump->kind = IR_JMP;
      jump->jmp.label = label_name;
      vector_push((*irs)->IRs, jump);
      *irs = new_ir_blocks();
      vector_push(blocks, *irs);

      gen_stmt(blocks, labels, irs, node->control.true_code);
      if (vector_size((*irs)->IRs))
      {
        vector_push(blocks, *irs = new_ir_blocks());
      }
      IR *end = calloc(1, sizeof(IR));
      end->kind = IR_LABEL;
      end->label.name = label_name;
      vector_push((*irs)->IRs, end);
      assert(vector_size((*irs)->IRs) == 1);
      vector_push(labels, *irs);
      return NULL;
    }
    case ND_FUNCCALL:
    {
      // Generate a CALL instruction.
      IR *ir = calloc(1, sizeof(IR));
      pr_debug("IR: generating CALL to %.*s", (int)node->token->len, node->token->str);

      // Evaluate each argument expression.
      Vector *args = vector_new();
      for (size_t i = 0; i < vector_size(node->func.expr); i++)
      {
        Node *arg = vector_peek_at(node->func.expr, i + 1);
        IR_REG *reg_val = gen_stmt(blocks, labels, irs, arg);
        vector_push(reg_val->used_list, ir);
        vector_push(args, reg_val);
      }
      pr_debug("IR: CALL args prepared: %zu", vector_size(args));

      ir->kind = IR_CALL;
      ir->call.func_name = node->token->str;
      ir->call.func_name_size = node->token->len;
      ir->call.args = args;
      // Node's type is the function result type after analysis.
      // If the function returns void, there is no destination register.
      // Fall back to int if missing (undeclared function defaults to int).
      Type *ret_ty = node->type;
      if (!ret_ty)
        ret_ty = alloc_type(TYPE_INT);
      pr_debug("IR: CALL return type kind=%d", (int)ret_ty->type);
      IR_REG *dst_reg_ptr = NULL;
      if (ret_ty->type != TYPE_VOID)
      {
        dst_reg_ptr = gen_reg();  // Register to receive the return value.
        dst_reg_ptr->reg_size = num2OpSize(size_of_real(ret_ty->type));
        ir->call.dst_reg = dst_reg_ptr;
        vector_push(dst_reg_ptr->used_list, ir);
      }
      char* call_state = "void";
      vector_push((*irs)->IRs, ir);
      if (dst_reg_ptr)
        call_state = "assigned";
      pr_debug("IR: CALL complete, dst vreg=%s", call_state);
      return dst_reg_ptr;
    }
    case ND_DISCARD_EXPR:
    {
      // Expression statement (discard the evaluation result).
      gen_stmt(blocks, labels, irs, node->lhs);
      return NULL;
    }
    case ND_BLOCK:
    {
      pr_debug("IR: ND_BLOCK enter");
      // Generate code for each statement in the block sequentially.
      for (NDBlock *b = node->func.stmt; b; b = b->next)
        gen_stmt(blocks, labels, irs, b->node);
      pr_debug("IR: ND_BLOCK done");
      return NULL;
    }
    case ND_FUNCDEF:
    {
      pr_debug("IR: ND_FUNCDEF enter");
      // Generate the function prologue.
      IR *prologue_ir = calloc(1, sizeof(IR));
      if (vector_size((*irs)->IRs))
        error_at(node->token->str, node->token->len, "invalid funcdef");
      prologue_ir->kind = IR_FUNC_PROLOGUE;
      vector_push((*irs)->IRs, prologue_ir);

      // Store arguments from registers to the variable area on the stack.
      for (size_t i = 0; i < vector_size(node->func.expr); i++)
      {
        Node *arg_node = vector_peek_at(node->func.expr, i + 1);
        if (arg_node->kind != ND_VARIABLE_ARGS)
        {
          IR_REG *addr_reg = gen_addr(blocks, labels, irs, arg_node);
          IR *ir = calloc(1, sizeof(IR));
          ir->kind = IR_STORE_ARG;
          ir->store_arg.dst_reg = addr_reg;
          vector_push(addr_reg->used_list, ir);
          ir->store_arg.arg_index = i;
          // Set the store width based on the argument's type
          ir->store_arg.size = num2OpSize(size_of_real(arg_node->type->type));
          vector_push((*irs)->IRs, ir);
        }
      }

      // Generate code for the function body.
      for (NDBlock *b = node->func.stmt; b; b = b->next)
      {
        pr_debug("IR: ND_FUNCDEF body stmt kind=%d",
                 b->node ? (int)b->node->kind : -1);
        gen_stmt(blocks, labels, irs, b->node);
      }

      // Generate the function epilogue.
      IR *epilogue_ir = calloc(1, sizeof(IR));
      epilogue_ir->kind = IR_FUNC_EPILOGUE;
      vector_push((*irs)->IRs, epilogue_ir);
      pr_debug("IR: ND_FUNCDEF done");
      return NULL;
    }
    case ND_DEREF:
    {
      // Dereference `*p`.
      // Evaluate the address of p.
      IR_REG *addr_reg_ptr = gen_stmt(blocks, labels, irs, node->lhs);
      // Load the value from that address.
      IR *ir = calloc(1, sizeof(IR));
      ir->kind = IR_LOAD;
      ir->mem.mem_reg = addr_reg_ptr;
      vector_push(addr_reg_ptr->used_list, ir);
      ir->mem.offset = 0;
      IR_REG *reg_ptr = gen_reg();
      reg_ptr->reg_size = num2OpSize(size_of_real(node->type->type));
      ir->mem.reg = reg_ptr;
      vector_push(reg_ptr->used_list, ir);
      ir->mem.size = num2OpSize(size_of_real(node->type->type));
      vector_push((*irs)->IRs, ir);
      return reg_ptr;
    }
    case ND_ADDR:
    {
      // Address-of `&v`.
      return gen_addr(blocks, labels, irs, node->lhs);
    }
    case ND_LOGICAL_NOT:
    case ND_NOT:
    case ND_UNARY_MINUS:
    {
      NodeKind kind = node->kind;
      Node *lhs = node->lhs;
      Type *result_type = node->type;

      pr_debug("IR: unary kind=%d", (int)kind);
      IR_REG *src_reg_ptr = gen_stmt(blocks, labels, irs, lhs);
      if (!src_reg_ptr)
      {
        if (node->token)
          error_at(node->token->str, node->token->len, "invalid unary operator");
        error_exit("invalid unary operator");
      }
      IR *ir = calloc(1, sizeof(IR));
      switch (kind)
      {
        case ND_LOGICAL_NOT: ir->kind = IR_NOT; break;
        case ND_NOT: ir->kind = IR_BIT_NOT; break;
        case ND_UNARY_MINUS: ir->kind = IR_NEG; break;
        default: unreachable(); break;
      }
      ir->un_op.src_reg = src_reg_ptr;
      vector_push(src_reg_ptr->used_list, ir);
      IR_REG *dst_reg_ptr = gen_reg();
      dst_reg_ptr->reg_size = num2OpSize(size_of_real(result_type->type));
      ir->un_op.dst_reg = dst_reg_ptr;
      vector_push(dst_reg_ptr->used_list, ir);
      vector_push((*irs)->IRs, ir);
      pr_debug("IR: unary queued");
      return dst_reg_ptr;
    }
    case ND_UNARY_PLUS:
    {
      // Unary plus `+x` does nothing.
      return gen_stmt(blocks, labels, irs, node->lhs);
    }
    case ND_EVAL:
    {
      // Convert int to bool
      IR_REG *lhs_reg = gen_stmt(blocks, labels, irs, node->lhs);
      IR_REG *rhs_reg = emit_imm(irs, 0);
      IR *ir = calloc(1, sizeof(IR));
      ir->kind = IR_NEQ;
      ir->bin_op.lhs_reg = lhs_reg;
      vector_push(lhs_reg->used_list, ir);
      ir->bin_op.rhs_reg = rhs_reg;
      vector_push(rhs_reg->used_list, ir);
      IR_REG *dst_reg_ptr = gen_reg();
      dst_reg_ptr->reg_size = SIZE_BYTE;  // bool
      ir->bin_op.dst_reg = dst_reg_ptr;
      vector_push(dst_reg_ptr->used_list, ir);
      vector_push((*irs)->IRs, ir);
      return dst_reg_ptr;
    }
    case ND_GOTO:
    {
      IR *ir = calloc(1, sizeof(IR));
      ir->kind = IR_JMP;
      ir->jmp.label = node->jump.label_name;
      vector_push((*irs)->IRs, ir);
      *irs = new_ir_blocks();
      vector_push(blocks, *irs);
      return NULL;
    }
    case ND_LABEL:
    {
      if (vector_size((*irs)->IRs))
      {
        vector_push(blocks, *irs = new_ir_blocks());
      }
      IR *ir = calloc(1, sizeof(IR));
      ir->kind = IR_LABEL;
      ir->label.name = node->jump.label_name;
      vector_push((*irs)->IRs, ir);
      assert(vector_size((*irs)->IRs) == 1);
      vector_push(labels, *irs);
      return gen_stmt(blocks, labels, irs, node->jump.statement_child);
    }
    case ND_PREDECREMENT:
    case ND_POSTDECREMENT:
    case ND_PREINCREMENT:
    case ND_POSTINCREMENT: return gen_incdec(blocks, labels, irs, node);
    case ND_ADD:
    case ND_SUB:
    case ND_MUL:
    case ND_DIV:
    case ND_REM:
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
      NodeKind kind = node->kind;
      Node *lhs = node->lhs;
      Node *rhs = node->rhs;
      Type *result_type = node->type;
      Type *lhs_type = lhs ? lhs->type : NULL;

      pr_debug("IR: binary kind=%d", (int)kind);
      // Binary operations.
      IR_REG *lhs_ptr = gen_stmt(blocks, labels, irs, lhs);
      IR_REG *rhs_ptr = gen_stmt(blocks, labels, irs, rhs);
      IR *ir = calloc(1, sizeof(IR));
      switch (kind)
      {
        case ND_ADD: ir->kind = IR_ADD; break;
        case ND_SUB: ir->kind = IR_SUB; break;
        case ND_MUL: ir->kind = result_type->is_signed ? IR_MUL : IR_MULU; break;
        case ND_DIV: ir->kind = result_type->is_signed ? IR_DIV : IR_DIVU; break;
        case ND_REM: ir->kind = result_type->is_signed ? IR_REM : IR_REMU; break;
        case ND_EQ: ir->kind = IR_EQ; break;
        case ND_NEQ: ir->kind = IR_NEQ; break;
        // For relational operators, signedness is determined by operands,
        // not the (boolean) result type. Using the result type here caused
        // us to always select the unsigned variants when the result was
        // TYPE_BOOL, which is incorrect for signed integers.
        case ND_LT: {
          bool signed_cmp = lhs_type && lhs_type->is_signed;
          ir->kind = signed_cmp ? IR_LT : IR_LTU;
          break;
        }
        case ND_LTE: {
          bool signed_cmp = lhs_type && lhs_type->is_signed;
          ir->kind = signed_cmp ? IR_LTE : IR_LTEU;
          break;
        }
        case ND_INCLUSIVE_OR: ir->kind = IR_OR; break;
        case ND_EXCLUSIVE_OR: ir->kind = IR_XOR; break;
        case ND_AND: ir->kind = IR_AND; break;
        case ND_LEFT_SHIFT:
          ir->kind = result_type->is_signed ? IR_SAL : IR_SHL;
          break;
        case ND_RIGHT_SHIFT:
          ir->kind = result_type->is_signed ? IR_SAR : IR_SHR;
          break;
        default: unreachable();
      }
      if (!lhs_ptr || !rhs_ptr)
      {
        if (node->token)
          error_at(node->token->str, node->token->len, "invalid operator");
        error_exit("invalid operator");
      }
      ir->bin_op.lhs_reg = lhs_ptr;
      vector_push(lhs_ptr->used_list, ir);
      ir->bin_op.rhs_reg = rhs_ptr;
      vector_push(rhs_ptr->used_list, ir);
      IR_REG *dst_reg_ptr = gen_reg();
      dst_reg_ptr->reg_size = num2OpSize(size_of_real(result_type->type));
      ir->bin_op.dst_reg = dst_reg_ptr;
      vector_push(dst_reg_ptr->used_list, ir);
      vector_push((*irs)->IRs, ir);
      pr_debug("IR: binary queued");
      return dst_reg_ptr;
    }
    case ND_LOGICAL_OR:
    case ND_LOGICAL_AND:
    {
      // Generate short-circuit evaluation without PHI by writing the
      // result into a single destination vreg on both paths.
      char *shortcut_label = gen_label();
      char *end_label = gen_label();

      IR_REG *dst = gen_reg();
      dst->reg_size = num2OpSize(size_of_real(node->type->type));

      // Evaluate LHS and jump to shortcut if it determines the result.
      IR_REG *lhs_ptr = gen_stmt(blocks, labels, irs, node->lhs);
      IR *ir1 = calloc(1, sizeof(IR));
      ir1->kind = (node->kind == ND_LOGICAL_OR) ? IR_JNE : IR_JE;
      ir1->jmp.label = shortcut_label;
      ir1->jmp.cond_reg = lhs_ptr;
      vector_push(lhs_ptr->used_list, ir1);
      vector_push((*irs)->IRs, ir1);
      *irs = new_ir_blocks();
      vector_push(blocks, *irs);

      // Path where short-circuit does not happen: result = (rhs != 0)
      IR_REG *rhs_ptr = gen_stmt(blocks, labels, irs, node->rhs);
      IR *rhs_cmp = calloc(1, sizeof(IR));
      rhs_cmp->kind = IR_NEQ;  // convert RHS to bool
      rhs_cmp->bin_op.lhs_reg = rhs_ptr;
      vector_push(rhs_ptr->used_list, rhs_cmp);
      IR_REG *zero = emit_imm(irs, 0);
      rhs_cmp->bin_op.rhs_reg = zero;
      vector_push(zero->used_list, rhs_cmp);
      IR_REG *rhs_bool = gen_reg();
      rhs_bool->reg_size = dst->reg_size;
      rhs_cmp->bin_op.dst_reg = rhs_bool;
      vector_push(rhs_bool->used_list, rhs_cmp);
      vector_push((*irs)->IRs, rhs_cmp);

      // Move computed bool into destination
      IR *mov_rhs_to_dst = calloc(1, sizeof(IR));
      mov_rhs_to_dst->kind = IR_MOV;
      mov_rhs_to_dst->mov.is_imm = false;
      mov_rhs_to_dst->mov.src_reg = rhs_bool;
      vector_push(rhs_bool->used_list, mov_rhs_to_dst);
      mov_rhs_to_dst->mov.dst_reg = dst;
      vector_push(dst->used_list, mov_rhs_to_dst);
      vector_push((*irs)->IRs, mov_rhs_to_dst);

      // Jump to end after computing RHS
      IR *jmp_end = calloc(1, sizeof(IR));
      jmp_end->kind = IR_JMP;
      jmp_end->jmp.label = end_label;
      vector_push((*irs)->IRs, jmp_end);
      *irs = new_ir_blocks();
      vector_push(blocks, *irs);

      // shortcut_label:
      if (vector_size((*irs)->IRs))
      {
        vector_push(blocks, *irs = new_ir_blocks());
      }
      IR *label_short = calloc(1, sizeof(IR));
      label_short->kind = IR_LABEL;
      label_short->label.name = shortcut_label;
      vector_push((*irs)->IRs, label_short);
      assert(vector_size((*irs)->IRs) == 1);
      vector_push(labels, *irs);

      // Path where short-circuit happens: result = (OR?1:0)
      IR *mov_short = calloc(1, sizeof(IR));
      mov_short->kind = IR_MOV;
      mov_short->mov.is_imm = true;
      mov_short->mov.imm_val = (node->kind == ND_LOGICAL_OR) ? 1 : 0;
      mov_short->mov.dst_reg = dst;
      vector_push(dst->used_list, mov_short);
      vector_push((*irs)->IRs, mov_short);

      // end_label:
      if (vector_size((*irs)->IRs))
      {
        vector_push(blocks, *irs = new_ir_blocks());
      }
      IR *label_end = calloc(1, sizeof(IR));
      label_end->kind = IR_LABEL;
      label_end->label.name = end_label;
      vector_push((*irs)->IRs, label_end);
      assert(vector_size((*irs)->IRs) == 1);
      vector_push(labels, *irs);

      return dst;
    }
    case ND_COMMA:
    {
      gen_stmt(blocks, labels, irs, node->lhs);
      return gen_stmt(blocks, labels, irs, node->rhs);
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
      vector_push((*irs)->IRs, builtin);
      return NULL;
    }
    case ND_DECLARATOR_LIST:
    {
      gen_stmt(blocks, labels, irs, node->lhs);
      gen_stmt(blocks, labels, irs, node->rhs);
      return NULL;
    }
    case ND_CAST: return gen_stmt(blocks, labels, irs, node->lhs);
    case ND_SIGN_EXTEND:
    case ND_ZERO_EXTEND:
    case ND_TRUNCATE:
    {
      if (node->kind == ND_TRUNCATE)
      {
        IR *ir = calloc(1, sizeof(IR));
        ir->kind = IR_MOV;
        IR_REG *src_reg = gen_stmt(blocks, labels, irs, node->lhs);
        ir->mov.src_reg = src_reg;
        vector_push(src_reg->used_list, ir);
        IR_REG *dst_reg = gen_reg();
        dst_reg->reg_size = num2OpSize(size_of_real(node->type->type));
        ir->mov.dst_reg = dst_reg;
        vector_push(dst_reg->used_list, ir);
        vector_push((*irs)->IRs, ir);
        return dst_reg;
      }
      IR *ir = calloc(1, sizeof(IR));
      switch (node->kind)
      {
        case ND_SIGN_EXTEND: ir->kind = IR_SIGN_EXTEND; break;
        case ND_ZERO_EXTEND: ir->kind = IR_ZERO_EXTEND; break;
        default: unreachable(); break;
      }
      IR_REG *src_reg = gen_stmt(blocks, labels, irs, node->lhs);
      ir->memsize.src_reg = src_reg;
      vector_push(src_reg->used_list, ir);
      IR_REG *dst_reg = gen_reg();
      dst_reg->reg_size = num2OpSize(size_of_real(node->type->type));
      ir->memsize.dst_reg = dst_reg;
      vector_push(dst_reg->used_list, ir);
      vector_push((*irs)->IRs, ir);
      return dst_reg;
    }
    default:
      error_exit("unexpected node kind: %d", node->kind);
      return 0;  // Unreachable
  }
}
