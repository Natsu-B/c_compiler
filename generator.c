// ------------------------------------------------------------------------------------
// generator
// ------------------------------------------------------------------------------------

#include "include/generator.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "include/error.h"
#include "include/type.h"
#include "include/variables.h"
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

// 移すサイズによってmv命令を変更する
char *mv_instruction_specifier(int size, bool is_sighed)
{
  char *tmp;
  if (is_sighed)
  {
    switch (size)
    {
      case 1:
      case 2: tmp = "movsx"; break;
      case 4: tmp = "movsxd"; break;
      case 8: tmp = "mov"; break;
      default: error_exit("unknown access size specifier"); break;
    }
  }
  else
  {
    switch (size)
    {
      case 1:
      case 2: tmp = "movzx"; break;
      case 4:
      case 8: tmp = "mov"; break;
      default: error_exit("unknown access size specifier"); break;
    }
  }
  return tmp;
}

// アクセスサイズの指定子を作成する
char *access_size_specifier(int size)
{
  char *tmp;
  switch (size)
  {
    case 1: tmp = "BYTE PTR"; break;
    case 2: tmp = "WORD PTR"; break;
    case 4: tmp = "DWORD PTR"; break;
    case 8: tmp = "QWORD PTR"; break;
    default: error_exit("unknown access size specifier"); break;
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
  switch (type)
  {
    case 1: tmp = "al"; break;
    case 2: tmp = "ax"; break;
    case 3: tmp = "eax"; break;
    case 4: tmp = "rax"; break;
    case 5: tmp = "cl"; break;
    case 6: tmp = "cx"; break;
    case 7: tmp = "ecx"; break;
    case 8: tmp = "rcx"; break;
    case 9: tmp = "dl"; break;
    case 10: tmp = "dx"; break;
    case 11: tmp = "edx"; break;
    case 12: tmp = "rdx"; break;
    case 13: tmp = "bl"; break;
    case 14: tmp = "bx"; break;
    case 15: tmp = "ebx"; break;
    case 16: tmp = "rbx"; break;
    case 17: tmp = "spl"; break;
    case 18: tmp = "sp"; break;
    case 19: tmp = "esp"; break;
    case 20: tmp = "rsp"; break;
    case 21: tmp = "bpl"; break;
    case 22: tmp = "bp"; break;
    case 23: tmp = "ebp"; break;
    case 24: tmp = "rbp"; break;
    case 25: tmp = "sil"; break;
    case 26: tmp = "si"; break;
    case 27: tmp = "esi"; break;
    case 28: tmp = "rsi"; break;
    case 29: tmp = "dil"; break;
    case 30: tmp = "di"; break;
    case 31: tmp = "edi"; break;
    case 32: tmp = "rdi"; break;
    case 33: tmp = "r8b"; break;
    case 34: tmp = "r8w"; break;
    case 35: tmp = "r8d"; break;
    case 36: tmp = "r8"; break;
    case 37: tmp = "r9b"; break;
    case 38: tmp = "r9w"; break;
    case 39: tmp = "r9d"; break;
    case 40: tmp = "r9"; break;
    case 41: tmp = "r10b"; break;
    case 42: tmp = "r10w"; break;
    case 43: tmp = "r10d"; break;
    case 44: tmp = "r10"; break;
    case 45: tmp = "r11b"; break;
    case 46: tmp = "r11w"; break;
    case 47: tmp = "r11d"; break;
    case 48: tmp = "r11"; break;
    case 49: tmp = "r12b"; break;
    case 50: tmp = "r12w"; break;
    case 51: tmp = "r12d"; break;
    case 52: tmp = "r12"; break;
    case 53: tmp = "r13b"; break;
    case 54: tmp = "r13w"; break;
    case 55: tmp = "r13d"; break;
    case 56: tmp = "r13"; break;
    case 57: tmp = "r14b"; break;
    case 58: tmp = "r14w"; break;
    case 59: tmp = "r14d"; break;
    case 60: tmp = "r14"; break;
    case 61: tmp = "r15b"; break;
    case 62: tmp = "r15w"; break;
    case 63: tmp = "r15d"; break;
    case 64: tmp = "r15"; break;
    default: error_exit_with_guard("unknown register type"); break;
  }
  return tmp;
}

void gen(Node *node);

// 左辺値をraxに出力する関数 raxが破壊されるため注意
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
      if (node->var->is_local)
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
    default: error_exit_with_guard("代入の左辺値が変数でありません");
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
    case ND_FUNCCALL:
      output_debug2("FUNC CALL");
      output_debug("start calling %.*s", (int)node->func_len, node->func_name);
      {
        NDBlock *pointer = node->expr;
        int i = 0;
        while (pointer)
        {
          gen(pointer->node);
          output_file("    pop rax");

          switch (++i)
          {
            case 1: output_file("    mov rdi, rax"); break;
            case 2: output_file("    mov rsi, rax"); break;
            case 3: output_file("    mov rdx, rax"); break;
            case 4: output_file("    mov rcx, rax"); break;
            case 5: output_file("    mov r8, rax"); break;
            case 6: output_file("    mov r9, rax"); break;
            default: error_exit_with_guard("too much arguments"); break;
          }
          pointer = pointer->next;
        }
      }
      output_file("    mov rax, rsp");
      output_file("    and rax, 15");
      output_file("    jnz .L_%d_unaligned", align_counter);
      output_file("    mov rax, 0");
      output_file("    call %.*s", (int)node->func_len, node->func_name);
      output_file("    jmp .L_%d_aligned", align_counter);
      output_file(".L_%d_unaligned:", align_counter);
      output_file("    sub rsp, 8");
      output_file("    mov rax, 0");
      output_file("    call %.*s", (int)node->func_len, node->func_name);
      output_file("    add rsp, 8");
      output_file(".L_%d_aligned:", align_counter);
      output_file("    push rax");
      align_counter++;
      output_debug("end calling %.*s", (int)node->func_len, node->func_name);
      return;

    case ND_ARRAY:
    case ND_DISCARD_EXPR:
      output_debug2("ND_ARRAY ND_DISCARD_EXPR");
      gen(node->lhs);
      if (node->kind == ND_DISCARD_EXPR)
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
      output_file("    push %ld", node->val);
      return;

    case ND_STRING:
    case ND_VAR:
      output_debug2("ND_STIRNG ND_VAR");
      gen_lval(node);
      if (node->type->type != TYPE_ARRAY && node->type->type != TYPE_STR)
      {
        output_file("    pop rax");
        output_file("    %s rax, %s [rax]",
                    mv_instruction_specifier(size_of(node->type), true),
                    access_size_specifier(size_of(node->type)));
        output_file("    push rax");
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
      output_file("    %s rax, %s [rax]",
                  mv_instruction_specifier(size_of(node->type), true),
                  access_size_specifier(size_of(node->type)));
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
      output_file("    %s rdi, %s [rax]",
                  mv_instruction_specifier(size_of(node->lhs->type),
                                           node->lhs->type->is_signed),
                  access_size_specifier(size_of(node->lhs->type)));
      if (node->kind == ND_POSTINCREMENT || node->kind == ND_POSTDECREMENT)
        output_file("    mov rsi, rdi");
      if (node->val)
      {
        if (node->kind == ND_PREINCREMENT || node->kind == ND_POSTINCREMENT)
          output_file("    add rdi, %ld", node->val);
        else
          output_file("    sub rdi, %ld", node->val);
      }
      else
      {
        if (node->kind == ND_PREINCREMENT || node->kind == ND_POSTINCREMENT)
          output_file("    inc rdi");
        else
          output_file("    dec rdi");
      }
      output_file("    mov %s [rax], %s",
                  access_size_specifier(size_of(node->type)),
                  chose_register(size_of(node->type), rdi));
      if (node->kind == ND_POSTINCREMENT || node->kind == ND_POSTDECREMENT)
        output_file("    push rsi");
      else
        output_file("    push rdi");
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
      output_file("    pop rdi");
      output_file("    %s rax, rdi", node->kind == ND_INCLUSIVE_OR ? "or"
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
                  node->kind == ND_LEFT_SHIFT ? "sal" : "sar");
      output_file("    push rax");
    }
      return;
    default: break;
  }

  output_debug2("arithmetic operands");
  gen(node->lhs);
  gen(node->rhs);

  output_file("    pop rdi");
  output_file("    pop rax");
  switch (node->kind)
  {
    case ND_ADD: output_file("    add rax, rdi"); break;
    case ND_SUB: output_file("    sub rax, rdi"); break;
    case ND_MUL: output_file("    imul rax, rdi"); break;
    case ND_DIV:
    case ND_IDIV:
      output_file("    cqo");
      output_file("    idiv rdi");
      if (node->kind == ND_IDIV)
        output_file("    mov rax, rdx");
      break;
    case ND_EQ:
    case ND_NEQ:
    case ND_LT:
    case ND_LTE:
      output_file("    cmp rax, rdi");
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
  {
    error_exit("ファイルに書き込めませんでした");
  }
  pr_debug("output file open");
  output_file(".intel_syntax noprefix");
  output_file(".data");

  // グローバル変数を書き込む
  Var *root = get_global_var();
  for (Var *pointer = root; pointer; pointer = pointer->next)
  {
    output_file("%.*s:", (int)pointer->len, pointer->name);
    switch (pointer->how2_init)
    {
      case reserved: unimplemented(); break;
      case init_zero:
        output_file(
            "    .zero %ld",
            size_of(pointer->type) *
                (pointer->type->type == TYPE_ARRAY ? pointer->type->size : 1));
        break;
      case init_string:
        output_file("    .string %.*s", (int)pointer->token->len,
                    pointer->token->str);
        break;
      default: unreachable(); break;
    }
  }

  // 関数を書き込む
  output_file(".text");
  for (FuncBlock *pointer = parsed; pointer; pointer = pointer->next)
  {
    Node *node = pointer->node;
    if (!node)
      continue;
    if (node->kind == ND_FUNCDEF)
    {
      output_file("\n.global %.*s", (int)node->func_len, node->func_name);
      output_file("%.*s:", (int)node->func_len, node->func_name);
      output_file("    push rbp");
      output_file("    mov rbp, rsp");
      output_file("    sub rsp, %lu", pointer->stacksize);
      int j = 0;
      for (NDBlock *pointer = node->expr; pointer; pointer = pointer->next)
      {
        if (pointer->node->kind != ND_VAR)
          error_exit_with_guard("invalid function arguments");
        switch (++j)
        {
          case 1:
            output_file("    mov [rbp-%lu], %s", pointer->node->var->offset,
                        chose_register(size_of(pointer->node->type), rdi));
            break;
          case 2:
            output_file("    mov [rbp-%lu], %s", pointer->node->var->offset,
                        chose_register(size_of(pointer->node->type), rsi));
            break;
          case 3:
            output_file("    mov [rbp-%lu], %s", pointer->node->var->offset,
                        chose_register(size_of(pointer->node->type), rdx));
            break;
          case 4:
            output_file("    mov [rbp-%lu], %s", pointer->node->var->offset,
                        chose_register(size_of(pointer->node->type), rcx));
            break;
          case 5:
            output_file("    mov [rbp-%lu], %s", pointer->node->var->offset,
                        chose_register(size_of(pointer->node->type), r8));
            break;
          case 6:
            output_file("    mov [rbp-%lu], %s", pointer->node->var->offset,
                        chose_register(size_of(pointer->node->type), r9));
            break;
          default: error_exit_with_guard("too much arguments"); break;
        }
      }

      for (NDBlock *pointer = node->stmt; pointer; pointer = pointer->next)
        gen(pointer->node);
      output_file("    pop rax");
      output_file("    leave");
      output_file("    ret");
      continue;
    }
    if (node->kind != ND_VAR)
      error_exit_with_guard("unreachable");
  }

  fclose(fout);
  pr_debug("complite generate");
}