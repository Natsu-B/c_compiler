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
  ND_NOP,            // Expected to be skipped
  ND_ADD,            // +
  ND_SUB,            // -
  ND_MUL,            // *
  ND_DIV,            // /
  ND_IDIV,           // %
  ND_EQ,             // ==
  ND_NEQ,            // !=
  ND_LT,             // <
  ND_LTE,            // <=
  ND_ASSIGN,         // =
  ND_ADDR,           // &
  ND_DEREF,          // *
  ND_LOGICAL_NOT,    // !
  ND_NOT,            // ~
  ND_UNARY_PLUS,     // + (unary)
  ND_UNARY_MINUS,    // - (unary)
  ND_PREINCREMENT,   // ++ ident
  ND_PREDECREMENT,   // -- ident
  ND_POSTINCREMENT,  // ident ++
  ND_POSTDECREMENT,  // ident --
  ND_FUNCDEF,        // Function definition
  ND_FUNCCALL,       // Function call
  ND_RETURN,         // Return statement
  ND_SIZEOF,         // sizeof unary-expression
  ND_TYPE_NAME,      // type-name
  ND_IF,             // if
  ND_ELIF,           // if else
  ND_FOR,            // for
  ND_WHILE,          // while
  ND_DO,             // do while
  ND_TERNARY,        // Ternary operator
  ND_LOGICAL_OR,     // ||
  ND_LOGICAL_AND,    // &&
  ND_INCLUSIVE_OR,   // |
  ND_EXCLUSIVE_OR,   // ^
  ND_AND,            // &
  ND_LEFT_SHIFT,     // <<
  ND_RIGHT_SHIFT,    // >>
  ND_ASSIGNMENT,     // Compound assignment (e.g., +=, -=)
  ND_VAR,            // Variable
  ND_ARRAY,          // Array
  ND_DOT,            // .
  ND_ARROW,          // ->
  ND_FIELD,          // Child of struct
  ND_NUM,            // Integer
  ND_BLOCK,          // Block
  ND_DISCARD_EXPR,   // Expression statement
  ND_STRING,         // String literal
  ND_GOTO,           // goto, continue, break
  ND_LABEL,          // goto label
  ND_CASE,           // Case statement
  ND_SWITCH,         // Switch statement
  ND_COMMA,          // Comma operator
  ND_CAST,           // Type cast
  ND_EVAL,           // Cast to bool
  ND_VARIABLE_ARGS,  // Variadic arguments
  ND_END,            // For debug use
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

// For debug use. Must be added if NodeKind is added.
#define NodeKindTable                                                         \
  "ND_NOP", "ND_ADD", "ND_SUB", "ND_MUL", "ND_DIV", "ND_IDIV", "ND_EQ",       \
      "ND_NEQ", "ND_LT", "ND_LTE", "ND_ASSIGN", "ND_ADDR", "ND_DEREF",        \
      "ND_LOGICAL_NOT", "ND_NOT", "ND_UNARY_PLUS", "ND_UNARY_MINUS",          \
      "ND_PREINCREMENT", "ND_PREDECREMENT", "ND_POSTINCREMENT",               \
      "ND_POSTDECREMENT", "ND_FUNCDEF", "ND_FUNCCALL", "ND_RETURN",           \
      "ND_SIZEOF", "ND_TYPE_NAME", "ND_IF", "ND_ELIF", "ND_FOR", "ND_WHILE",  \
      "ND_DO", "ND_TERNARY", "ND_LOGICAL_OR", "ND_LOGICAL_AND",               \
      "ND_INCLUSIVE_OR", "ND_EXCLUSIVE_OR", "ND_AND", "ND_LEFT_SHIFT",        \
      "ND_RIGHT_SHIFT", "ND_ASSIGNMENT", "ND_VAR", "ND_ARRAY", "ND_DOT",      \
      "ND_ARROW", "ND_FIELD", "ND_NUM", "ND_BLOCK", "ND_DISCARD_EXPR",        \
      "ND_STIRNG", "ND_GOTO", "ND_LABEL", "ND_CASE", "ND_SWITCH", "ND_COMMA", \
      "ND_CAST", "ND_EVAL"
extern const char *nodekindlist[ND_END];

struct Node
{
  NodeKind kind;  // Type of node
  Type *type;     // Type
  Token *token;   // Token from which it was parsed

  Node *lhs;            // Left-hand side
  Node *rhs;            // Right-hand side
  // struct
  // {                // For loop
  Node *chs;            // Used only for ternary operator
  size_t child_offset;  // Used for ND_DOT, ND_ARROW, offset of its child
                        // };

  // struct
  // {                                  // For if, for, while, switch statements
  GTLabel *name;     // Label name, also used for goto
  Node *condition;   // Condition for judgment
  Node *true_code;   // Code executed when true
  Node *false_code;  // Code executed when false in if-else statements
                     // struct
                     // {                // For loop
  Node *init;        // Initialization code, e.g., int i = 0
  Node *update;      // Code executed at each step, e.g., i++
  // };
  Vector *case_list;  // Contains Node* of cases in switch statement
                      // };
                      // struct
                      // struct
  // {                   // For ND_BLOCK, ND_FUNCCALL, ND_FUNCDEF
  Vector *expr;       // Used in ND_FUNCCALL, ND_FUNCDEF
  NDBlock *stmt;      // Used in ND_BLOCK, ND_FUNCDEF
  char *func_name;    // Used in ND_FUNCCALL, ND_FUNCDEF, function name
  size_t func_len;  // Used only in ND_FUNCCALL, ND_FUNCDEF, length of function
                    // name
  // };
  long long val;  // Value for ND_NUM, size of pointer type for ND_POST/PRE
                  // INCREMENT/DECREMENT, 0 for numeric type
                  // For variables (ND_VAR)
  bool is_new;         // Whether it's a newly defined variable
  Var *var;            // Variable information
                       // };
                       // struct
                       // {                      // For string type ND_STRING
  char *literal_name;  // Name to access string literal
                       // };
                       // struct
  // {                            // For ND_GOTO, ND_LABEL, ND_CASE, ND_DEFAULT
  Node *statement_child;  // statement
  char *label_name;       // Label name (not used in ND_CASE, ND_DEFAULT)
  bool is_case;           // For ND_CASE, ND_DEFAULT
  size_t case_num;        // Which case number in switch statement (0-indexed)
  GTLabel *switch_name;   // Label name of switch statement
  long constant_expression;  // The 'n' part of 'case n:'
  // };
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

#endif  // PARSER_C_COMPILER