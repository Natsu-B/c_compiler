// ------------------------------------------------------------------------------------
// generator
// ------------------------------------------------------------------------------------

#include "include/generator.h"

#ifdef SELF_HOST
#include "test/compiler_header.h"
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

#include "include/error.h"
#include "include/type.h"
#include "include/vector.h"

#define error_exit_with_guard(fmt, ...)                                     \
  do                                                                        \
  {                                                                         \
    output_file("error detected at %s:%d:%s() exit...", __FILE__, __LINE__, \
                __func__);                                                  \
    error_exit(fmt, ##__VA_ARGS__);                                         \
  } while (0)

#if DEBUG
#define output_debug(fmt, ...) \
  output_file("# %s:%d:%s()" fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__);
#define output_debug2(fmt, ...) \
  output_file("# %s:%d:%s()" fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__);
#elif defined(DEBUG)
#define output_debug(fmt, ...) output_file("# " fmt, ##__VA_ARGS__);
#define output_debug2(fmt, ...)
#else
#define output_debug(fmt, ...)
#define output_debug2(fmt, ...)
#endif

static FILE *fout;

#define output_file(fmt, ...)               \
  do                                        \
  {                                         \
    fprintf(fout, fmt "\n", ##__VA_ARGS__); \
  } while (0)

// Creates the access size specifier
char *access_size_specifier(int size)
{
  char *tmp;
  switch (size)
  {
    case 1: tmp = "BYTE PTR"; break;
    case 2: tmp = "WORD PTR"; break;
    case 4: tmp = "DWORD PTR"; break;
    case 8: tmp = "QWORD PTR"; break;
    default: error_exit("unknown access size specifier: %d", size); break;
  }
  return tmp;
}

typedef enum
{
  rax,
  rcx,
  rdx,
  rbx,
  rsp,
  rbp,
  rsi,
  rdi,
  r8,
  r9,
  r10,
  r11,
  r12,
  r13,
  r14,
  r15,
} register_type;

char *chose_register(int size, register_type reg_type)
{
  switch (size)
  {
    case 1:
    case 2: break;
    case 4: size = 3; break;
    case 8: size = 4; break;
    default: error_exit_with_guard("unknown register size"); break;
  }
  char *tmp;
  int type = reg_type * 4 + size;
  char *register_names[64] = {
      "al",   "ax",   "eax",  "rax", "cl",   "cx",   "ecx",  "rcx",
      "dl",   "dx",   "edx",  "rdx", "bl",   "bx",   "ebx",  "rbx",
      "spl",  "sp",   "esp",  "rsp", "bpl",  "bp",   "ebp",  "rbp",
      "sil",  "si",   "esi",  "rsi", "dil",  "di",   "edi",  "rdi",
      "r8b",  "r8w",  "r8d",  "r8",  "r9b",  "r9w",  "r9d",  "r9",
      "r10b", "r10w", "r10d", "r10", "r11b", "r11w", "r11d", "r11",
      "r12b", "r12w", "r12d", "r12", "r13b", "r13w", "r13d", "r13",
      "r14b", "r14w", "r14d", "r14", "r15b", "r15w", "r15d", "r15"};
  if (type <= 64)
    tmp = register_names[type - 1];
  else
    error_exit_with_guard("unknown register type");

  return tmp;
}

// Changes the mv instruction depending on the size to be moved
// Output up to mv rax
char *mv_instruction_specifier(int size, bool is_sighed, register_type reg)
{
  char *tmp = calloc(1, 11 /* movsxd rax */);
  size_t copied = 0;
  if (is_sighed)
  {
    switch (size)
    {
      case 1:
      case 2:
        strcpy(tmp, "movsx");
        copied += 5;
        break;
      case 4:
        strcpy(tmp, "movsxd");
        copied += 6;
        break;
      case 8:
        strcpy(tmp, "mov");
        copied += 3;
        break;
      default: error_exit("unknown access size specifier"); break;
    }
  }
  else
  {
    switch (size)
    {
      case 1:
      case 2:
        strcpy(tmp, "movzx");
        copied += 5;
        break;
      case 4:
      case 8:
        strcpy(tmp, "mov");
        copied += 3;
        break;
      default: error_exit("unknown access size specifier"); break;
    }
  }
  tmp[copied] = ' ';
  if (!is_sighed && size == 4)
    strcpy(tmp + copied + 1, chose_register(size, reg));
  else
    strcpy(tmp + copied + 1, chose_register(8, reg));
  return tmp;
}

void call_built_in_func(Node *node)
{
  if (node->token->len == 7 && !strncmp(node->token->str, "__asm__", 7))
  {
    Node *asm_string = vector_pop(node->expr);
    if (asm_string->kind != ND_NOP || vector_size(node->expr))
      unreachable();
    output_file("%.*s", (int)asm_string->token->len, asm_string->token->str);
    return;
  }
  unreachable();
}

void gen_global_var(Node *node)
{
  if (node->kind != ND_VAR)
    unreachable();
  // in order to support name mangling for static variables declared inside
  // functions we should use node->var->name
  if (!(node->var->storage_class_specifier & 1 << 2))
    output_file("    .globl %.*s", (int)node->var->len, node->var->name);
  output_file("    .type %.*s, @object", (int)node->var->len, node->var->name);
  output_file("    .size %.*s, %lu", (int)node->var->len, node->var->name,
              size_of(node->var->type));
  output_file("%.*s:", (int)node->var->len, node->var->name);
}

void gen(Node *node);

// Function to output lvalue to rax. Be careful as rax will be destroyed.
void gen_lval(Node *node)
{
  output_debug2("enter gen_lval");
  switch (node->kind)
  {
    case ND_STRING:
      output_file("    lea rax, [rip+OFFSET FLAT:%s]", node->literal_name);
      output_file("    push rax");
      break;
    case ND_VAR:
      if (node->var->is_local && !(node->var->storage_class_specifier & 1 << 1))
        output_file("    lea rax, [rbp-%d]", (int)node->var->offset);
      else
        output_file("    lea rax,  [rip+%.*s]", (int)node->var->len,
                    node->var->name);
      output_file("    push rax");
      break;
    case ND_DEREF: gen(node->lhs); break;
    case ND_DOT:
    case ND_ARROW:
      gen_lval(node->lhs);
      output_file("    pop rax");
      if (node->kind == ND_ARROW)
        output_file("    mov rax, QWORD PTR [rax]");  // dereference
      output_file("    add rax, %lu", node->child_offset);
      output_file("    push rax");
      break;
    default:
      error_exit_with_guard(
          "The left-hand side of the assignment is not a variable.");
  }
  output_debug2("exit gen_lval");
}

int align_counter;

void gen(Node *node)
{
  output_debug2("enter gen");
  if (!node)
    return;

  switch (node->kind)
  {
    case ND_NOP: return;
    case ND_FUNCCALL:
    {
      output_debug2("FUNC CALL");
      output_debug("start calling %.*s", (int)node->token->len,
                   node->token->str);
      for (size_t i =
               (vector_size(node->expr) > 6 ? 6 : vector_size(node->expr));
           i >= 1; i--)
      {
        gen(vector_peek_at(node->expr, i));

        switch (i)
        {
          case 1: output_file("    pop rdi"); break;
          case 2: output_file("    pop rsi"); break;
          case 3: output_file("    pop rdx"); break;
          case 4: output_file("    pop rcx"); break;
          case 5: output_file("    pop r8"); break;
          case 6: output_file("    pop r9"); break;
          default: break;
        }
      }
      output_file("    mov rax, rsp");
      output_file("    and rax, 15");
      output_file("    push r12");
      output_file("    mov r12, 0");
      output_file("    jz .L_%d_aligned", align_counter);
      output_file("    mov r12, 8");
      output_file("    sub rsp, 8");
      output_file(".L_%d_aligned:", align_counter);
      size_t stack_args = 0;
      if (vector_size(node->expr) > 6)
        stack_args = vector_size(node->expr) - 6;
      if (stack_args)
      {
        // output_file("    sub rsp, %lu", (stack_args + 1) / 2 * 2 * 8);
        for (size_t i = 7; i <= vector_size(node->expr); i++)
          // Equivalent to pushing node value to stack
          gen(vector_peek_at(node->expr, vector_size(node->expr) - i + 7));
      }
      output_file("    mov rax, 0");
      output_file("    call %.*s", (int)node->token->len, node->token->str);
      output_file("    add rsp, r12");
      if (stack_args)
        output_file("    add rsp, %lu", stack_args * 8);
      output_file("    pop r12");
      output_file("    push rax");
      align_counter++;
      output_debug("end calling %.*s", (int)node->token->len, node->token->str);
      return;
    }
    case ND_BUILTINFUNC: call_built_in_func(node); return;
    case ND_ARRAY:
    case ND_DISCARD_EXPR:
      output_debug2("ND_ARRAY ND_DISCARD_EXPR");
      gen(node->lhs);
      output_file("    add rsp, 8");
      return;

    case ND_BLOCK:
      output_debug2("ND_BLOCK");
      for (NDBlock *pointer = node->stmt; pointer; pointer = pointer->next)
        gen(pointer->node);
      return;

    case ND_IF:
    case ND_ELIF:
    {
      output_debug2("ND_IF ND_ELIF");
      output_debug("start %s block", node->kind == ND_IF ? "if" : "elif");
      gen(node->condition);
      output_file("    pop rax");
      output_file("    cmp rax, 0");
      output_file("    je .L%s%s", node->kind == ND_IF ? "endif" : "elseif",
                  node->name->name);
      gen(node->true_code);
      if (node->kind == ND_ELIF)
      {
        output_file("    jmp .Lendif%s", node->name->name);
        output_file(".Lelseif%s:", node->name->name);
        gen(node->false_code);
      }
      output_file(".Lendif%s:", node->name->name);
      output_debug("end %s block", node->kind == ND_IF ? "if" : "elif");
    }
      return;

    case ND_WHILE:
    case ND_FOR:
    {
      output_debug2("ND_WHILE ND_FOR");
      const char *loop_name = node->kind == ND_WHILE ? "while" : "for";
      output_debug("start %s block", loop_name);
      if (node->kind == ND_FOR)
        gen(node->init);
      output_file(".Lbegin%s%.*s:", loop_name, (int)node->name->len,
                  node->name->name);
      gen(node->condition);
      output_file("    pop rax");
      output_file("    cmp rax, 0");
      output_file("    je .Lend%s%.*s", loop_name, (int)node->name->len,
                  node->name->name);
      gen(node->true_code);
      if (node->kind == ND_FOR)
        gen(node->update);
      output_file("    jmp .Lbegin%s%.*s", loop_name, (int)node->name->len,
                  node->name->name);
      output_file(".Lend%s%s:", loop_name, node->name->name);
      output_debug("end %s block", loop_name);
    }
      return;
    case ND_DO:
      output_debug2("ND_DO");
      output_file(".Lbegindo%.*s:", (int)node->name->len, node->name->name);
      gen(node->true_code);
      gen(node->condition);
      output_file("    pop rax");
      output_file("    cmp rax, 0");
      output_file("    jne .Lbegindo%.*s", (int)node->name->len,
                  node->name->name);
      output_file(".Lenddo%.*s:", (int)node->name->len, node->name->name);
      return;
    case ND_RETURN:
      output_debug2("ND_RETURN");
      gen(node->lhs);
      output_file("    pop rax");
      output_file("    leave");
      output_file("    ret");
      return;

    case ND_NUM:
      output_debug2("ND_NUM");
      output_file("    push %lld", node->val);
      return;

    case ND_STRING:
    case ND_VAR:
    case ND_DOT:
    case ND_ARROW:
      output_debug2("ND_STIRNG ND_VAR ND_DOT ND_ARROW");
      if (node->kind == ND_DOT || node->kind == ND_ARROW ||
          node->type->type != TYPE_STRUCT)
      {
        gen_lval(node);
        if (node->type->type != TYPE_ARRAY && node->type->type != TYPE_STR)
        {
          output_file("    pop rax");
          output_file("    %s, %s [rax]",
                      mv_instruction_specifier(size_of(node->type),
                                               node->type->is_signed, rax),
                      access_size_specifier(size_of(node->type)));
          output_file("    push rax");
        }
      }
      return;

    case ND_ASSIGN:
      output_debug2("ND_ASSIGN");
      gen_lval(node->lhs);
      gen(node->rhs);
      output_file("    pop rdi");
      output_file("    pop rax");
      output_file("    mov %s [rax], %s",
                  access_size_specifier(size_of(node->type)),
                  chose_register(size_of(node->type), rdi));
      output_file("    push rdi");
      return;

    case ND_ADDR:
      output_debug2("ND_ADDR");
      gen_lval(node->lhs);
      return;

    case ND_DEREF:
      output_debug2("ND_DEREF");
      gen(node->lhs);
      output_file("    pop rax");
      output_file("    %s, %s [rax]",
                  mv_instruction_specifier(size_of(node->type),
                                           node->type->is_signed, rax),
                  access_size_specifier(size_of(node->type)));
      output_file("    push rax");
      return;
    case ND_UNARY_PLUS: gen(node->lhs); return;
    case ND_UNARY_MINUS:
      gen(node->lhs);
      output_file("    pop rax");
      output_file("    neg rax");
      output_file("    push rax");
      return;
    case ND_LOGICAL_NOT:
      output_file("    pop rax");
      output_file("    cmp rax, 0");
      output_file("    sete al");
      output_file("    movzx rax, al");
      output_file("    push rax");
      return;
    case ND_NOT:
      output_file("    pop rax");
      output_file("    not rax");
      output_file("    push rax");
      return;
    case ND_GOTO:
      output_debug2("ND_GOTO");
      output_file("    jmp %s", node->label_name);
      return;
    case ND_LABEL:
      output_debug2("ND_LABEL");
      output_file("%s:", node->label_name);
      gen(node->statement_child);
      return;
    case ND_PREINCREMENT:
    case ND_PREDECREMENT:
    case ND_POSTINCREMENT:
    case ND_POSTDECREMENT:
      output_debug2("ND '++' or '--'");
      gen_lval(node->lhs);
      output_file("    pop rax");
      output_file("    %s, %s [rax]",
                  mv_instruction_specifier(size_of(node->lhs->type),
                                           node->lhs->type->is_signed, r10),
                  access_size_specifier(size_of(node->lhs->type)));
      if (node->kind == ND_POSTINCREMENT || node->kind == ND_POSTDECREMENT)
        output_file("    mov r11, r10");
      if (node->val)
      {
        if (node->kind == ND_PREINCREMENT || node->kind == ND_POSTINCREMENT)
          output_file("    add r10, %lld", node->val);
        else
          output_file("    sub r10, %lld", node->val);
      }
      else
      {
        if (node->kind == ND_PREINCREMENT || node->kind == ND_POSTINCREMENT)
          output_file("    inc r10");
        else
          output_file("    dec r10");

        if (node->type->type == TYPE_BOOL)
        {
          output_file("    cmp r10, 0");
          output_file("    setne dil");
          output_file("    movzx r10, dil");
        }
      }
      output_file("    mov %s [rax], %s",
                  access_size_specifier(size_of(node->type)),
                  chose_register(size_of(node->type), r10));
      if (node->kind == ND_POSTINCREMENT || node->kind == ND_POSTDECREMENT)
        output_file("    push r11");
      else
        output_file("    push r10");
      return;
    case ND_CASE:
    {
      output_debug2("ND_CASE");
      output_file(".Lswitch%.*s_%lu:", (int)node->switch_name->len,
                  node->switch_name->name, node->case_num);
      gen(node->statement_child);
      return;
    }
    case ND_SWITCH:
    {
      output_debug2("ND_SWITCH");
      gen(node->condition);
      output_file("    pop rdi");
      GTLabel *switch_label = node->name;
      for (size_t i = 1; i <= vector_size(node->case_list); i++)
      {
        Node *case_child = vector_peek_at(node->case_list, i);
        if (case_child->is_case)
        {
          output_file("    cmp rdi, %ld", case_child->constant_expression);
          output_file("    je .Lswitch%.*s_%lu", (int)switch_label->len,
                      switch_label->name, i - 1);
        }
        else
          output_file("    jmp .Lswitch%.*s_%lu", (int)switch_label->len,
                      switch_label->name, i - 1);
      }
      gen(node->true_code);
      output_file(".Lendswitch%.*s:", (int)switch_label->len,
                  switch_label->name);
    }
      return;
    case ND_TERNARY:
    {
      output_debug2("ND_TERNARY");
      gen(node->lhs);
      output_file("    pop rax");
      output_file("    cmp rax, 0");
      output_file("    je .Lfalseternary%s", node->name->name);
      gen(node->chs);
      output_file("    jmp .Lendternary%s", node->name->name);
      output_file(".Lfalseternary%s:", node->name->name);
      gen(node->rhs);
      output_file(".Lendternary%s:", node->name->name);
    }
      return;
    case ND_LOGICAL_AND:
    case ND_LOGICAL_OR:
    {
      output_debug2("ND_LOGICAL AND/OR");
      gen(node->lhs);
      output_file("    pop rax");
      output_file("    cmp rax, 0");
      output_file("    %s .Lfalselogical%s%s",
                  node->kind == ND_LOGICAL_AND ? "je" : "jne",
                  node->kind == ND_LOGICAL_AND ? "and" : "or",
                  node->name->name);
      gen(node->rhs);
      output_file("    jmp .Lendlogical%s%s",
                  node->kind == ND_LOGICAL_AND ? "and" : "or",
                  node->name->name);
      output_file(
          ".Lfalselogical%s%s:", node->kind == ND_LOGICAL_AND ? "and" : "or",
          node->name->name);
      output_file("    push rax");
      output_file(
          ".Lendlogical%s%s:", node->kind == ND_LOGICAL_AND ? "and" : "or",
          node->name->name);
    }
      return;
    case ND_INCLUSIVE_OR:
    case ND_AND:
    case ND_EXCLUSIVE_OR:
    {
      output_debug2("ND_INCLUSIVE_OR / ND_AND");
      gen(node->lhs);
      gen(node->rhs);
      output_file("    pop rax");
      output_file("    pop r10");
      output_file("    %s rax, r10", node->kind == ND_INCLUSIVE_OR ? "or"
                                     : node->kind == ND_AND        ? "and"
                                                                   : "xor");
      output_file("    push rax");
    }
      return;
    case ND_LEFT_SHIFT:
    case ND_RIGHT_SHIFT:
    {
      output_debug2("ND_LEFT/RIGHT_SHIFT");
      gen(node->lhs);
      gen(node->rhs);
      output_file("    pop rcx");
      output_file("    pop rax");
      output_file("    %s rax, cl",
                  node->lhs->type->is_signed
                      ? node->kind == ND_LEFT_SHIFT ? "sal" : "sar"
                  : node->kind == ND_LEFT_SHIFT ? "shl"
                                                : "shr");
      output_file("    push rax");
    }
      return;
    case ND_COMMA:
      gen(node->lhs);
      output_file("    add rsp, 8");
      gen(node->rhs);
      return;
    case ND_CAST: gen(node->lhs); return;
    case ND_EVAL:
      gen(node->lhs);
      output_file("    pop rax");
      output_file("    cmp rax, 0");
      output_file("    setne al");
      output_file("    movzx rax, al");
      output_file("    push rax");
      return;
    default: break;
  }

  output_debug2("arithmetic operands");
  gen(node->lhs);
  gen(node->rhs);

  output_file("    pop r10");
  output_file("    pop rax");
  switch (node->kind)
  {
    case ND_ADD: output_file("    add rax, r10"); break;
    case ND_SUB: output_file("    sub rax, r10"); break;
    case ND_MUL: output_file("    imul rax, r10"); break;
    case ND_DIV:
    case ND_IDIV:
      output_file("    cqo");
      output_file("    idiv r10");
      if (node->kind == ND_IDIV)
        output_file("    mov rax, rdx");
      break;
    case ND_EQ:
    case ND_NEQ:
    case ND_LT:
    case ND_LTE:
      output_file("    cmp rax, r10");
      output_file("    set%s al", node->kind == ND_EQ    ? "e"
                                  : node->kind == ND_NEQ ? "ne"
                                  : node->kind == ND_LT  ? "l"
                                                         : "le");
      output_file("    movzb rax, al");
      break;
    default: error_exit_with_guard("unreachable"); break;
  }
  output_file("    push rax");
}

void generator(FuncBlock *parsed, char *output_filename)
{
  pr_debug("start generator");

  pr_debug("output filename: %.*s", strlen(output_filename), output_filename);
  fout = fopen(output_filename, "w");
  if (fout == NULL)
    error_exit("cannot written to file");
  pr_debug("output file open");
  output_file("    .intel_syntax noprefix\n");

  // strings
  Vector *string_list = get_string_list();
  for (size_t i = 1; i <= vector_size(string_list); i++)
  {
    if (i == 1)
      output_file("    .section .rodata");
    Var *string = vector_peek_at(string_list, i);
    output_file("%.*s:", (int)string->len, string->name);
    output_file("    .string \"%.*s\"", (int)string->token->len,
                string->token->str);
  }

  // Write functions
  for (FuncBlock *pointer = parsed; pointer; pointer = pointer->next)
  {
    Node *node = pointer->node;
    if (!node)
      continue;
    if (node->kind == ND_FUNCDEF)
    {
      output_file("\n    .text\n");
      if (!(node->storage_class_specifier & 1 << 2))
        output_file("    .global %.*s", (int)node->token->len,
                    node->token->str);
      output_file("    .type   %.*s, @function", (int)node->token->len,
                  node->token->str);
      output_file("%.*s:", (int)node->token->len, node->token->str);
      output_file("    push rbp");
      output_file("    mov rbp, rsp");
      output_file("    sub rsp, %lu", pointer->stacksize);
      int j = 0;
      for (size_t i = 1; i <= vector_size(node->expr); i++)
      {
        Node *param = vector_peek_at(node->expr, i);
        if (param->kind != ND_VAR)
          error_exit_with_guard("invalid function arguments");
        output_debug("argument: %lu", i);
        switch (++j)
        {
          case 1:
            output_file("    mov [rbp-%lu], %s", param->var->offset,
                        chose_register(size_of(param->type), rdi));
            break;
          case 2:
            output_file("    mov [rbp-%lu], %s", param->var->offset,
                        chose_register(size_of(param->type), rsi));
            break;
          case 3:
            output_file("    mov [rbp-%lu], %s", param->var->offset,
                        chose_register(size_of(param->type), rdx));
            break;
          case 4:
            output_file("    mov [rbp-%lu], %s", param->var->offset,
                        chose_register(size_of(param->type), rcx));
            break;
          case 5:
            output_file("    mov [rbp-%lu], %s", param->var->offset,
                        chose_register(size_of(param->type), r8));
            break;
          case 6:
            output_file("    mov [rbp-%lu], %s", param->var->offset,
                        chose_register(size_of(param->type), r9));
            break;
          default:
            output_file("    mov rax, [rbp+%lu]", 8 + 8 * (i - 6));
            output_file("    mov [rbp-%lu], %s", param->var->offset,
                        chose_register(size_of(param->type), rax));
            break;
        }
      }

      for (NDBlock *pointer = node->stmt; pointer; pointer = pointer->next)
        gen(pointer->node);
      output_file("    pop rax");
      output_file("    leave");
      output_file("    ret");
      continue;
    }
    else if (node->kind == ND_BUILTINFUNC)
      call_built_in_func(node);
    else if (node->kind == ND_VAR)
    {
      if (node->var->storage_class_specifier & 1 << 1)
        continue;
      output_file("\n    .section    .bss");
      gen_global_var(node);
      output_file("    .zero %lu\n", size_of(node->var->type));
    }
    else if (node->kind == ND_ASSIGN && node->lhs->kind == ND_VAR)
    {
      output_file("\n    .section .data");
      gen_global_var(node->lhs);
      switch (node->rhs->kind)
      {
        case ND_NUM:
          switch (size_of(node->lhs->type))
          {
            case 1: output_file("    .byte %lld", node->rhs->val); break;
            case 2: output_file("    .word %lld", node->rhs->val); break;
            case 4: output_file("    .long %lld", node->rhs->val); break;
            case 8: output_file("    .quad %lld", node->rhs->val); break;
            default: unreachable();
          }
          break;
        case ND_ADDR:
          output_file("    .quad %.*s", (int)node->rhs->lhs->var->len,
                      node->rhs->lhs->var->name);
          break;
        case ND_STRING:
          output_file("    .quad %s", node->rhs->literal_name);
          break;
        default: unimplemented();
      }
    }
    else if (node->kind != ND_NOP)
      error_exit_with_guard("Unreachable");
  }

  fclose(fout);
  pr_debug("Generation complete.");
}