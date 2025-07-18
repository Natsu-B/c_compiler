// ------------------------------------------------------------------------------------
// tokenizer
// ------------------------------------------------------------------------------------
#include "include/tokenizer.h"

#ifdef SELF_HOST
#include "test/compiler_header.h"
#else
#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#endif

#include "include/debug.h"
#include "include/error.h"
#include "include/preprocessor.h"
#include "include/vector.h"

Token *token_head;
Token *token;      // The actual token
Token *token_old;  // The token after token

const char *tokenkindlist[TK_END] = {TokenKindTable};

void fix_token_head()
{
  while (token->kind == TK_IGNORABLE || token->kind == TK_ILB ||
         token->kind == TK_LINEBREAK || token->kind == TK_EOF)
  {
    token = token->next;
    if (!token->next)
      break;
  }
  token_head = token;
}

Token *get_token_head()
{
  return token_head;
}

// Advance the token, ignoring TK_IGNORABLE, TK_LIB, and TK_LINEBREAK
Token *token_next()
{
  token_old = token;
  do
  {
    // if (token->kind != TK_IGNORABLE && token->kind != TK_ILB &&
    //     token->kind != TK_LINEBREAK)
    //   token_old = token;
    token = token->next;
  } while (token->kind == TK_IGNORABLE || token->kind == TK_ILB ||
           token->kind == TK_LINEBREAK);
  return token_old;
}

// Change the position of the token to the position of the argument
// Use with caution as it is quite dangerous
void set_token(Token *next)
{
  token = next;
}

Token *get_token()
{
  return token;
}

// If the next token is of the argument kind and the token after that is
// TK_RESERVED and equal to the argument reserved, consume it. Otherwise, return
// NULL.
Token *consume_token_if_next_matches(TokenKind kind, char reserved)
{
  Token *next = token;
  do
  {
    next = next->next;
  } while (next->kind == TK_IGNORABLE || next->kind == TK_ILB ||
           next->kind == TK_LINEBREAK);
  if (token->kind == kind && next->kind == TK_RESERVED &&
      next->str[0] == reserved)
    return token_next();
  return NULL;
}

Token *peek(char *op, TokenKind kind)
{
  if (token->kind != kind || strlen(op) != token->len ||
      memcmp(op, token->str, token->len))
    return NULL;
  return token;
}

// If the next token is the argument symbol, read it. Otherwise, return NULL.
// However, TK_IGNORABLE is excluded.
Token *consume(char *op, TokenKind kind)
{
  if (peek(op, kind))
    return token_next();
  return NULL;
}

// If the next token is the argument symbol, read it. Otherwise, call error_at.
Token *expect(char *op, TokenKind kind)
{
  Token *result = consume(op, kind);
  if (!result)
    error_at(token->str, token->len, "Token is not %s.", op);
  return result;
}

// Get the previous token
Token *get_old_token()
{
  return token_old;
}

Token *peek_ident()
{
  long long tmp;
  if (token->kind != TK_IDENT || is_number(&tmp))
    return NULL;
  return token;
}

Token *consume_ident()
{
  if (peek_ident())
    return token_next();
  return NULL;
}

Token *expect_ident()
{
  Token *expect = consume_ident();
  if (!expect)
    error_at(token->str, token->len, "Token is not an ident");
  return expect;
}

Token *consume_string()
{
  Token *return_token = NULL;
  while (token->kind == TK_STRING)
  {
    if (!return_token)
      return_token = token_next();
    else
    {
      Token *string = token_next();
      return_token->next = token;
      char *tmp = malloc(return_token->len + string->len - 2 /* duplicate " */);
      strncpy(tmp, return_token->str, return_token->len);
      strncpy(tmp + return_token->len - 1, string->str + 1, string->len - 1);
      return_token->str = tmp;
      return_token->len += (string->len - 2);
    }
  }
  return return_token;
}

Token *consume_char()
{
  if (token->kind == TK_CHAR)
    return token_next();
  return NULL;
}

bool is_number(long long *result)
{
  if (token->kind != TK_IDENT || !isdigit(token->str[0]))
    return false;
  char *ptr;
  *result = strtoll(token->str, &ptr, 10);
  if (ptr - token->str != (int)token->len)
    return false;
  return true;
}

// If the next token is an integer, read it. Otherwise, return an error.
bool consume_number(long long *value)
{
  if (!is_number(value))
    return false;
  token_next();
  return true;
}

// Returns true if the token is the last (TK_EOF), otherwise returns false.
bool at_eof()
{
  return token->kind == TK_EOF;
}

// Whether the given argument constitutes a token (alphanumeric and '_')
int is_alnum(char c)
{
  return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') ||
         ('0' <= c && c <= '9') || (c == '_');
}

// Function to create a new token
Token *new_token(TokenKind kind, char *str)
{
  Token *new = calloc(1, sizeof(Token));
  new->kind = kind;
  new->str = str;
  return new;
}

Token *tokenize_once(char *input, char **end)
{
  Token *cur;
  // Newline: isspace also processes '\n', so place it before that.
  if (*input == '\n')
  {
    cur = new_token(TK_LINEBREAK, input++);
    cur->len = 1;
    *end = input;
    return cur;
  }

  size_t space_counter = 0;
  while (isspace(*input) && *input != '\n')
  {
    space_counter++;
    input++;
  }
  if (space_counter)
  {
    cur = new_token(TK_IGNORABLE, input - space_counter);
    cur->len = space_counter;
    *end = input;
    return cur;
  }

  // Skip comments
  if (!strncmp(input, "//", 2))
  {
    input++;
    size_t i = 2;
    while (*++input != '\n')
      i++;
    cur = new_token(TK_IGNORABLE, input - i);
    cur->len = i;
    *end = input;
    return cur;
  }
  if (!strncmp(input, "/*", 2))
  {
    char *comment_end = strstr(input + 2, "*/");
    if (!comment_end)
      error_at(input, 1, "Comment is not closed.");
    cur = new_token(TK_IGNORABLE, input);
    char *pointer = input;
    while (pointer <= comment_end)
    {
      if (*++pointer == '\n')
      {
        cur->len = pointer - input;
        cur = new_token(TK_LINEBREAK, pointer);
        cur->len = 1;
        cur = new_token(TK_IGNORABLE, pointer);
        input = pointer;
      }
    }
    cur->len = comment_end + 2 - input;
    *end = comment_end + 2;
    return cur;
  }

  if (*input == '#')
  {
    cur = new_token(TK_DIRECTIVE, input);
    cur->len = 1;
    *end = input + 1;
    return cur;
  }

  if (strchr("+-*/()=!<>;{},&[].\\|%?:~^", *input))
  {
    if (*input == '\\' && *(input + 1) == '\n')
    {
      cur = new_token(TK_ILB, input);
      cur->len = 2;
      input += 2;
      *end = input;
      return cur;
    }
    cur = new_token(TK_RESERVED, input);
    // For "==", "<=", ">=", "!=", "&&", "||", "->", "++", "--", "*=", "/=",
    // "%=",
    // "+=", "-=", "<<=", ">>=", "&=", "^=", "|="
    if ((*(input + 2) == '=' && ((*(input + 1) == '<' && *(input) == '<') ||
                                 (*(input + 1) == '>' && *(input) == '>'))) ||
        (*(input + 2) == '.' && *(input + 1) == '.' && *input == '.'))
    {
      pr_debug2("find RESERVED token: %.3s", input);
      cur->len = 3;
      input += 3;
    }
    else if ((*(input + 1) == '=' &&
              (*input == '<' || *input == '>' || *input == '!' ||
               *input == '=' || *input == '*' || *input == '/' ||
               *input == '%' || *input == '+' || *input == '-' ||
               *input == '&' || *input == '^' || *input == '|')) ||
             (*(input + 1) == '>' && *input == '>') ||
             (*(input + 1) == '<' && *input == '<') ||
             (*(input + 1) == '&' && *input == '&') ||
             (*(input + 1) == '|' && *input == '|') ||
             (*(input + 1) == '+' && *input == '+') ||
             (*(input + 1) == '-' && *input == '-') ||
             (*(input + 1) == '>' && *input == '-'))
    {
      pr_debug2("find RESERVED token: %.2s", input);
      cur->len = 2;
      input += 2;
    }
    else
    {
      pr_debug2("find RESERVED token: %.1s", input);
      cur->len = 1;
      input++;
    }
    *end = input;
    return cur;
  }

  // String detection
  if (*input == '"')
  {
    int i = 0;  // Size of the string (excluding ")
    while (*(++input) != '"')
      i++;
    input++;  // Advance to the end
    cur = new_token(TK_STRING, input - i - 2);
    cur->len = i + 2;
    *end = input;
    return cur;
  }

  // Character detection
  if (*input == '\'')
  {
    input++;  // Skip opening quote
    cur = new_token(TK_CHAR, input);
    cur->str = input;
    cur->len = 1;
    if (*input++ == '\\')
    {  // Handle escape sequences
      input++;
      cur->len = 2;
      if (!(*input == 'n' || *input == 't' || *input == '\\' ||
            *input == '\'' || *input == '"' || *input == '0'))
        error_at(input, 1, "Unknown escape sequence.");
    }
    if (*input != '\'')
    {
      error_at(input, 1, "Unterminated character literal.");
    }
    input++;  // Skip closing quote
    *end = input;
    return cur;
  }

  // If it's only alphanumeric and '_', consider it a variable or reserved word
  int i = 0;
  while (is_alnum(*input))
  {
    i++;
    input++;
  }
  if (i)
  {
    cur = new_token(TK_IDENT, input - i);
    cur->len = i;
    *end = input;
    return cur;
  }
  error_at(input, 1, "Failed to tokenize.");
  return NULL;
}

// Function to tokenize
Token *tokenizer(char *input, char *end, Token *next_token)
{
  pr_debug("start tokenizer...");
  Token head;
  head.next = NULL;
  Token *cur = &head;
  Vector *nest_list = vector_new();
  while (end ? input != end : *input)
  {
    char *end = NULL;
    cur->next = tokenize_once(input, &end);
    input = end;
    cur = cur->next;

    if (cur->kind == TK_DIRECTIVE)
    {
      size_t counter = 0;
      char *tmp = input;
      while (isspace(*tmp) && *tmp != '\n')
        tmp++;
      while (is_alnum(*tmp))
      {
        counter++;
        tmp++;
      }
      // Conditional_Inclusion (#if #endif etc.) to speed up
      if ((counter == 2 && !strncmp(tmp - counter, "if", 2)) ||
          (counter == 5 && !strncmp(tmp - counter, "ifdef", 5)) ||
          (counter == 6 && !strncmp(tmp - counter, "ifndef", 6)))
      {
        Vector *new = vector_new();
        vector_push(Conditional_Inclusion_List, new);
        vector_push(nest_list, new);
        vector_push(new, cur);
      }
      else if ((counter == 4 && (!strncmp(tmp - counter, "else", 4) ||
                                 !strncmp(tmp - counter, "elif", 4))) ||
               (counter == 7 && !strncmp(tmp - counter, "elifdef", 7)) ||
               (counter == 8 && !strncmp(tmp - counter, "elifndef", 8)))
        vector_push(vector_peek(nest_list), cur);
      else if (counter == 5 && !strncmp(tmp - counter, "endif", 5))
        vector_push(vector_pop(nest_list), cur);
    }
  }

  cur->next = new_token(TK_EOF, input);
  cur->next->next = next_token;

  pr_debug("Tokenization complete.");
  vector_free(nest_list);
#ifdef DEBUG
  print_tokenize_result(head.next);
#endif

  token = head.next;
  return token;
}