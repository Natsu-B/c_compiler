// ------------------------------------------------------------------------------------
// builtin function
// ------------------------------------------------------------------------------------

#include "include/builtin.h"

#include "include/error.h"
#include "include/parser.h"
#include "include/tokenizer.h"
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
    expect("(", TK_RESERVED);
    Node* child_node = assignment_expression();
    if (!child_node)
      unreachable();
    if (child_node->kind != ND_STRING)
      error_at(child_node->token->str, child_node->token->len,
               "__asm__ function must takes char strings");
    child_node->token = parse_string_literal(child_node->token);
    child_node->kind = ND_NOP;
    vector_push((**node).func.expr, child_node);
    expect(")", TK_RESERVED);
    return true;
  }
  return false;
}