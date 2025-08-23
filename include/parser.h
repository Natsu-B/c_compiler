#ifndef PARSER_C_COMPILER
#define PARSER_C_COMPILER

#ifdef SELF_HOST
#include "../test/compiler_header.h"
#else
#include <stddef.h>
#endif

#include "tokenizer.h"
#include "type.h"
#include "vector.h"

typedef struct Node Node;
typedef struct Var Var;
typedef struct Type Type;
typedef struct GTLabel GTLabel;
typedef struct NDBlock NDBlock;
typedef struct FuncBlock FuncBlock;

typedef enum
{
  ND_NOP,              // Expected to be skipped
  ND_ADD,              // +
  ND_SUB,              // -
  ND_MUL,              // *
  ND_DIV,              // /
  ND_IDIV,             // %
  ND_EQ,               // ==
  ND_NEQ,              // !=
  ND_LT,               // <
  ND_LTE,              // <=
  ND_ASSIGN,           // =
  ND_ADDR,             // &
  ND_DEREF,            // *
  ND_LOGICAL_NOT,      // !
  ND_NOT,              // ~
  ND_UNARY_PLUS,       // + (unary)
  ND_UNARY_MINUS,      // - (unary)
  ND_PREINCREMENT,     // ++ ident
  ND_PREDECREMENT,     // -- ident
  ND_POSTINCREMENT,    // ident ++
  ND_POSTDECREMENT,    // ident --
  ND_FUNCDEF,          // Function definition
  ND_FUNCCALL,         // Function call
  ND_BUILTINFUNC,      // Built in function call
  ND_RETURN,           // Return statement
  ND_SIZEOF,           // sizeof unary-expression
  ND_TYPE_NAME,        // type-name
  ND_IF,               // if
  ND_ELIF,             // if else
  ND_FOR,              // for
  ND_WHILE,            // while
  ND_DO,               // do while
  ND_TERNARY,          // Ternary operator
  ND_LOGICAL_OR,       // ||
  ND_LOGICAL_AND,      // &&
  ND_INCLUSIVE_OR,     // |
  ND_EXCLUSIVE_OR,     // ^
  ND_AND,              // &
  ND_LEFT_SHIFT,       // <<
  ND_RIGHT_SHIFT,      // >>
  ND_ASSIGNMENT,       // Compound assignment (e.g., +=, -=)
  ND_VAR,              // Variable
  ND_ARRAY,            // Array
  ND_DOT,              // .
  ND_ARROW,            // ->
  ND_FIELD,            // Child of struct
  ND_NUM,              // Integer
  ND_BLOCK,            // Block
  ND_DISCARD_EXPR,     // Expression statement
  ND_STRING,           // String literal
  ND_GOTO,             // goto, continue, break
  ND_LABEL,            // goto label
  ND_CASE,             // Case statement
  ND_SWITCH,           // Switch statement
  ND_COMMA,            // Comma operator
  ND_INITIALIZER,      // array or struct initializer
  ND_CAST,             // Type cast
  ND_EVAL,             // Cast to bool
  ND_DECLARATOR_LIST,  // declarator list
  ND_VARIABLE_ARGS,    // Variadic arguments
  ND_END,              // For debug use
} NodeKind;

/**
 *  Label Naming Convention
 *
 *  if statement
 *  .Lend_N_XXX
 *  .Lelse_N_XXX
 *
 *  while statement
 *  .Lbegin_N_XXX
 *  .Lend_N_XXX
 *
 *  for statement
 *  .Lbegin_N_XXX
 *  .Lend_N_XXX
 *
 *  goto statement
 *  .Lgoto_YYY_XXX
 *
 *  XXX is the called function name, YYY is the label name
 *  N is an integer from 0~, e.g., begin_1_XXX end_1_XXX
 */
// Struct to manage goto labels
struct GTLabel
{
  NodeKind kind;  // Name of the Node
  char *name;     // Variable name, e.g., _0_main
  size_t len;     // Length of variable name
};

enum function_type
{
  FUNC_USER_DEFINED,  // non builtin functions
  FUNC_ASM,           // builtin __asm__
};

// For debug use. Must be added if NodeKind is added.
#define NodeKindTable                                                         \
  "ND_NOP", "ND_ADD", "ND_SUB", "ND_MUL", "ND_DIV", "ND_IDIV", "ND_EQ",       \
      "ND_NEQ", "ND_LT", "ND_LTE", "ND_ASSIGN", "ND_ADDR", "ND_DEREF",        \
      "ND_LOGICAL_NOT", "ND_NOT", "ND_UNARY_PLUS", "ND_UNARY_MINUS",          \
      "ND_PREINCREMENT", "ND_PREDECREMENT", "ND_POSTINCREMENT",               \
      "ND_POSTDECREMENT", "ND_FUNCDEF", "ND_FUNCCALL", "ND_BUILTINFUNC",      \
      "ND_RETURN", "ND_SIZEOF", "ND_TYPE_NAME", "ND_IF", "ND_ELIF", "ND_FOR", \
      "ND_WHILE", "ND_DO", "ND_TERNARY", "ND_LOGICAL_OR", "ND_LOGICAL_AND",   \
      "ND_INCLUSIVE_OR", "ND_EXCLUSIVE_OR", "ND_AND", "ND_LEFT_SHIFT",        \
      "ND_RIGHT_SHIFT", "ND_ASSIGNMENT", "ND_VAR", "ND_ARRAY", "ND_DOT",      \
      "ND_ARROW", "ND_FIELD", "ND_NUM", "ND_BLOCK", "ND_DISCARD_EXPR",        \
      "ND_STIRNG", "ND_GOTO", "ND_LABEL", "ND_CASE", "ND_SWITCH", "ND_COMMA", \
      "ND_INITIALIZER", "ND_CAST", "ND_EVAL", "ND_DECLARATOR_LIST",           \
      "ND_VARIABLE_ARGS"
extern const char *nodekindlist[ND_END];

struct Node
{
  NodeKind kind;
  Type *type;
  Token *token;

  Node *lhs;
  Node *rhs;

  union
  {
    // if, for, while, do-while, switch, ternary, logical or/and
    struct
    {
      GTLabel *label;
      Node *condition;
      Node *true_code;
      Node *false_code;     // for if-else
      Node *init;           // for for-loop
      Node *update;         // for for-loop
      Node *ternary_child;  // for ternary
      Vector *case_list;    // for switch
    } control;

    // function definition/call
    struct
    {
      Vector *expr;
      NDBlock *stmt;
      uint8_t storage_class_specifier;
      enum function_type builtin_func;
    } func;

    // variable
    struct
    {
      Var *var;
      bool is_new_var;
      bool is_array_top;
    } variable;

    // number literal
    long long num_val;

    // string literal
    char *literal_name;

    // goto, label, case
    struct
    {
      Node *statement_child;
      char *label_name;
      bool is_case;
      size_t case_num;
      GTLabel *switch_name;
      long constant_expression;
    } jump;

    // struct/union member access
    size_t child_offset;

    // initializer
    struct
    {
      Vector *init_list;
      Node *assigned;
      bool is_top_initializer;
    } initialize;
  };
};

// Struct to manage expressions within arguments or blocks
struct NDBlock
{
  NDBlock *next;  // Next variable
  Node *node;     // Expression within the block
};

// Struct to manage expressions within a function
struct FuncBlock
{
  FuncBlock *next;   // Next variable
  Node *node;        // Expression within the function
  size_t stacksize;  // Stack size in bytes
};

FuncBlock *get_funcblock_head();
Node *new_node(NodeKind kind, Node *lhs, Node *rhs, Token *token);
Node *new_node_num(long long val);
Node *constant_expression();
FuncBlock *parser();
Node *declarator_no_side_effect(Type **type, uint8_t storage_class_specifier);
Node *assignment_expression();

// walk child node
// Note: node->lhs, node->rhs, and ND_BLOCK are not walked
#define visit_children(node, visitor, ...)                                     \
  do                                                                           \
  {                                                                            \
    if (!(node))                                                               \
      break;                                                                   \
    if ((node)->kind == ND_TERNARY)                                            \
      (node)->control.ternary_child =                                          \
          visitor((node)->control.ternary_child, ##__VA_ARGS__);               \
    if ((node)->kind == ND_IF || (node)->kind == ND_ELIF ||                    \
        (node)->kind == ND_FOR || (node)->kind == ND_WHILE ||                  \
        (node)->kind == ND_DO || (node)->kind == ND_SWITCH)                    \
    {                                                                          \
      (node)->control.condition =                                              \
          visitor((node)->control.condition, ##__VA_ARGS__);                   \
      (node)->control.true_code =                                              \
          visitor((node)->control.true_code, ##__VA_ARGS__);                   \
      (node)->control.false_code =                                             \
          visitor((node)->control.false_code, ##__VA_ARGS__);                  \
      (node)->control.init = visitor((node)->control.init, ##__VA_ARGS__);     \
      (node)->control.update = visitor((node)->control.update, ##__VA_ARGS__); \
    }                                                                          \
    if ((node)->kind == ND_LABEL || (node)->kind == ND_CASE)                   \
      (node)->jump.statement_child =                                           \
          visitor((node)->jump.statement_child, ##__VA_ARGS__);                \
    if ((node)->kind == ND_FUNCCALL || (node)->kind == ND_FUNCDEF)             \
      for (size_t i = 1; i <= vector_size((node)->func.expr); i++)             \
      {                                                                        \
        Node *result =                                                         \
            visitor(vector_peek_at((node)->func.expr, i), ##__VA_ARGS__);      \
        vector_replace_at((node)->func.expr, i, result);                       \
      }                                                                        \
    if ((node)->kind == ND_INITIALIZER)                                        \
      for (size_t i = 1; i <= vector_size((node)->initialize.init_list); i++)  \
        visitor(vector_peek_at((node)->initialize.init_list, i),               \
                ##__VA_ARGS__);                                                \
  } while (0)

#endif  // PARSER_C_COMPILER
