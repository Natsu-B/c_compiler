#ifndef CONDITIONAL_INCLUSION_C_COMPILER
#define CONDITIONAL_INCLUSION_C_COMPILER

#include "vector.h"

typedef enum
{
  token_if,
  token_ifdef,
  token_ifndef,
} if_directive;

typedef enum
{
  CPPTK_NOP,                // unused
  CPPTK_Question,           // ?
  CPPTK_Colon,              //  :
  CPPTK_Logical_OR,         // ||
  CPPTK_Logical_AND,        // &&
  CPPTK_Inclusive_OR,       // |
  CPPTK_Exclusive_OR,       // ^
  CPPTK_AND,                // &
  CPPTK_Equality,           // ==
  CPPTK_NEquality,          // !=
  CPPTK_LessThan,           // <
  CPPTK_LessThanEq,         // <=
  CPPTK_GreaterThan,        // >
  CPPTK_GreaterThanEq,      // >=
  CPPTK_LeftShift,          // <<
  CPPTK_RightShift,         // >>
  CPPTK_Plus,               // +
  CPPTK_Minus,              // -
  CPPTK_Mul,                //*
  CPPTK_Div,                // /
  CPPTK_DivReminder,        // %
  CPPTK_UnaryPlus,          // +
  CPPTK_UnaryMinus,         // -
  CPPTK_NOT,                // !
  CPPTK_Bitwise,            // ~
  CPPTK_Integer,            // Numeric value
  CPPTK_Parentheses_Start,  // (
  CPPTK_Parentheses_End,    // )
  CPPTK_Reserved,
} conditional_inclusion_type;

#define CPPTK_list                                                        \
  "CPPTK_Reserved", "CPPTK_Question", "CPPTK_Colon", "CPPTK_Logical_OR",  \
      "CPPTK_Logical_AND", "CPPTK_Inclusive_OR", "CPPTK_Exclusive_OR",    \
      "CPPTK_AND", "CPPTK_Equality", "CPPTK_NEquality", "CPPTK_LessThan", \
      "CPPTK_LessThanEq", "CPPTK_GreaterThan", "CPPTK_GreaterThanEq",     \
      "CPPTK_LeftShift", "CPPTK_RightShift", "CPPTK_Plus", "CPPTK_Minus", \
      "CPPTK_Mul", "CPPTK_Div", "CPPTK_DivReminder", "CPPTK_UnaryPlus",   \
      "CPPTK_UnaryMinus", "CPPTK_NOT", "CPPTK_Bitwise", "CPPTK_Integer",  \
      "CPPTK_Parentheses_Start", "CPPTK_Parentheses_End"

typedef struct conditional_inclusion_token conditional_inclusion_token;

struct conditional_inclusion_token
{
  conditional_inclusion_type type;
  long long num;  // long long value when numeric
};

void conditional_inclusion(if_directive type, Vector *conditional_list);

#endif