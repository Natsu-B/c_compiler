// ------------------------------------------------------------------------------------
// builtin function
// ------------------------------------------------------------------------------------

#include "include/builtin.h"

#include "include/error.h"
#include "include/parser.h"
#include "include/type.h"

bool is_builtin_function(Node** node, Token* token, bool is_root)
{
  (void)is_root;  // unused now
  *node = calloc(1, sizeof(Node));
  (**node).token = token;
  if (token && token->len == 7 && !strncmp(token->str, "__asm__", 7))
  {
    (**node).kind = ND_BUILTINFUNC;
    (**node).func.expr = vector_new();
    (**node).func.builtin_func = FUNC_ASM;
    expect("(", TK_RESERVED);
    Node* child_node = assignment_expression();
    if (!child_node)
      unreachable();
    if (child_node->kind != ND_STRING)
      error_at(child_node->token->str, child_node->token->len,
               "__asm__ function must takes char strings");
    child_node->kind = ND_NOP;
    vector_push((**node).func.expr, child_node);
    expect(")", TK_RESERVED);
    return true;
  }
  return false;
}

// parse escape sequences
char* parse_string_literal(char* str, size_t* len)
{
  char* string = malloc(*len);
  size_t string_len = 0;
  for (size_t i = 0; i < *len; i++)
  {
    if (*(str + i) == '\\')
    {
      switch ((str + i)[1])
      {
        case 'n': *(string + string_len) = '\n'; break;
        case 't': *(string + string_len) = '\t'; break;
        case '\\': *(string + string_len) = '\\'; break;
        case '\'': *(string + string_len) = '\''; break;
        case '"': *(string + string_len) = '\"'; break;
        case '0': *(string + string_len) = '\0'; break;
        case 'e': *(string + string_len) = '\e'; break;
        default: error_at(str + i, 2, "unknown control character found");
      }
      ++i;
      ++string_len;
    }
    else
      *(string + string_len++) = *(str + i);
  }
  *len = string_len;
  return string;
}