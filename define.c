// ------------------------------------------------------------------------------------
// definition manager
// ------------------------------------------------------------------------------------

#include "include/define.h"

#ifdef SELF_HOST
#include "test/compiler_header.h"
#else
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#endif

#include "include/error.h"
#include "include/preprocessor.h"
#include "include/tokenizer.h"
#include "include/vector.h"

Vector *object_like_macro_list;
Vector *function_like_macro_list;

int find_macro_name_without_hide_set(Token *identifier, Vector *hide_set,
                                     Vector **argument_list,
                                     Vector **token_string, Token **token,
                                     size_t *location);

// If the argument string is defined as an object-like macro, it returns 1.
// If it is defined as a function-like macro, it returns 2. Otherwise, it
// returns 0.
inline int find_macro_name_all(Token *identifier)
{
  return find_macro_name_without_hide_set(identifier, NULL, NULL, NULL, NULL,
                                          NULL);
}

// If the argument string is defined as an object-like macro, it returns 1.
// If it is defined as a function-like macro, it returns 2. Otherwise, it
// returns 0. If it returns 1 or 2, the token string is stored in token_string,
// the macro name is stored in token, and the location of the macro in the
// (object or function)_macro_list is stored in location. (if they are non-null)
// If it returns 2, the function argument list is stored in argument_list.
// However, this excludes those that have already been expanded.
int find_macro_name_without_hide_set(Token *identifier, Vector *hide_set,
                                     Vector **argument_list,
                                     Vector **token_string, Token **token,
                                     size_t *location)
{
  // hide set
  if (hide_set)
    for (size_t j = 1; j <= vector_size(hide_set); j++)
    {
      Token *hide = vector_peek_at(hide_set, j);
      if (hide->len == identifier->len &&
          !strncmp(hide->str, identifier->str, hide->len))
        return 0;
    }
  // object like macro
  for (size_t i = 1; i <= vector_size(object_like_macro_list); i++)
  {
    object_like_macro_storage *tmp = vector_peek_at(object_like_macro_list, i);
    if (tmp->identifier->len == identifier->len &&
        !strncmp(tmp->identifier->str, identifier->str, tmp->identifier->len))
    {
      if (token_string)
        *token_string = tmp->token_string;
      if (token)
        *token = tmp->identifier;
      if (location)
        *location = i;
      return 1;
    }
  }
  // function like macro
  for (size_t i = 1; i <= vector_size(function_like_macro_list); i++)
  {
    function_like_macro_storage *tmp =
        vector_peek_at(function_like_macro_list, i);
    if (tmp->identifier->len == identifier->len &&
        !strncmp(tmp->identifier->str, identifier->str, tmp->identifier->len))
    {
      if (token_string)
        *token_string = tmp->token_string;
      if (argument_list)
        *argument_list = tmp->arguments;
      if (token)
        *token = tmp->identifier;
      if (location)
        *location = i;
      return 2;
    }
  }
  return 0;
}

void add_object_like_macro(Vector *token_list)
{  // #define identifier token-string_opt
  Token *identifier = vector_shift(token_list);
  Vector *token_list_before = NULL;
  int result = find_macro_name_without_hide_set(identifier, NULL, NULL,
                                                &token_list_before, NULL, NULL);
  if (result == 2 ||
      (result == 1 && !vector_compare(token_list, token_list_before)))
    error_at(identifier->str, identifier->len,
             "Identifier %s is already defined.");
  object_like_macro_storage *new = malloc(sizeof(object_like_macro_storage));
  new->identifier = identifier;
  new->token_string = token_list;
  if (!object_like_macro_list)
    object_like_macro_list = vector_new();
  vector_push(object_like_macro_list, new);
}

void add_function_like_macro(Vector *token_list)
{  // #define identifier(identifier_opt, identifier_opt) token-stirng_opt
  Token *identifier = vector_shift(token_list);
  Vector *token_list_before = NULL;
  int result = find_macro_name_without_hide_set(identifier, NULL, NULL,
                                                &token_list_before, NULL, NULL);
  if (result == 1 ||
      (result == 2 && !vector_compare(token_list, token_list_before)))
    error_at(identifier->str, identifier->len,
             "Identifier %s is already defined.");

  function_like_macro_storage *new =
      malloc(sizeof(function_like_macro_storage));
  new->identifier = identifier;
  new->arguments = vector_shift(token_list);
  new->token_string = token_list;
  if (!function_like_macro_list)
    function_like_macro_list = vector_new();
  vector_push(function_like_macro_list, new);
}

extern char *File_Name;
extern size_t File_Line;
extern long long include_level;

bool ident_replacement_recursive(Token *token, Vector *hide_set)
{
  if (token->len == 8)
  {
    if (!strncmp(token->str, "__FILE__", 8))
    {
      size_t file_name_len = strlen(File_Name);
      char *file_name = malloc(file_name_len);  // no need for NULL terminator
      strncpy(file_name, File_Name, file_name_len);
      token->kind = TK_STRING;
      token->len = file_name_len + 2;
      token->str = file_name;
      return true;
    }
    if (!strncmp(token->str, "__LINE__", 8))
    {
      char *file_line_str = malloc(7 * sizeof(char));
      int file_line_len = snprintf(file_line_str, 7, "%lu", File_Line);
      if (file_line_len > 6 || file_line_len < 0)
      {
        int size = file_line_len;
        file_line_str = realloc(file_line_str, file_line_len + 1);
        file_line_len =
            snprintf(file_line_str, file_line_len + 1, "%lu", File_Line);
        if (file_line_len < 0 || file_line_len + 1 > size)
          error_exit("Failed to preprocess __LINE__.");
      }
      token->len = file_line_len;
      token->str = file_line_str;
      return true;
    }
    int is_date = !strncmp(token->str, "__DATE__", 8);
    int is_time = !strncmp(token->str, "__TIME__", 8);
    if (is_date || is_time)
    {
      time_t current_time = time(NULL);
      if (current_time == -1)
        error_exit("Failed to get time.");
      // asctime
      char *asctime_str = asctime(localtime(&current_time));
      char *time_str;
      if (is_date)
      {
        time_str = malloc(13 * sizeof(char));
        memcpy(time_str + 1, asctime_str + 4, 7);
        memcpy(time_str + 8, asctime_str + 20, 4);
        token->len = 13;
      }
      if (is_time)
      {
        time_str = malloc(10 * sizeof(char));
        memcpy(time_str + 1, asctime_str + 11, 8);
        token->len = 10;
      }
      token->kind = TK_STRING;
      time_str[0] = time_str[token->len - 1] = '"';
      token->str = time_str;
      return true;
    }
  }
  if (token->len == 17)
  {
    if (!strncmp(token->str, "__INCLUDE_LEVEL__", 17))
    {
      char *file_line_str = malloc(7 * sizeof(char));
      int file_line_len = snprintf(file_line_str, 7, "%lld", include_level);
      if (file_line_len > 6 || file_line_len < 0)
      {
        int size = file_line_len;
        file_line_str = realloc(file_line_str, file_line_len + 1);
        file_line_len =
            snprintf(file_line_str, file_line_len + 1, "%lu", File_Line);
        if (file_line_len < 0 || file_line_len + 1 > size)
          error_exit("Failed to preprocess __INCLUDE_LEVEL__.");
      }
      token->kind = TK_STRING;
      token->len = file_line_len;
      token->str = file_line_str;
      return true;
    }
  }

  Vector *token_string = NULL;
  Token *token_identifier;
  Vector *argument_list = NULL;
  size_t is_defined = find_macro_name_without_hide_set(
      token, hide_set, &argument_list, &token_string, &token_identifier, NULL);
  if (is_defined == 1)
  {  // if it is an object-like macro
    vector_push(hide_set, token_identifier);
    Token *tmp = token;
    Token *old = token;
    Token *next = token->next;
    if (token_string && vector_size(token_string))
    {
      for (size_t i = 1; i <= vector_size(token_string); i++)
      {
        Token *replace_token = vector_peek_at(token_string, i);
        memcpy(tmp, replace_token, sizeof(Token));
        tmp->next = malloc(sizeof(Token));
        old = tmp;
        tmp = tmp->next;
      }
      old->next = next;
      while (token != next)
      {
        ident_replacement_recursive(token, hide_set);
        token = token->next;
      }
      return true;
    }
    token_void(token);
    return true;
  }
  else if (is_defined == 2 &&
           token_next_not_ignorable(token)->kind == TK_RESERVED &&
           token_next_not_ignorable(token)->str[0] == '(')
  {  // if it is a function-like macro
    token_void(token);
    // Vector containing the actual argument list of the function-like macro
    Vector *argument_real_list = vector_new();
    vector_push(hide_set, token_identifier);
    Token *const old = token;  // points to the token to be replaced (void)
    Token *next = token_next_not_ignorable_void(token);
    token_void(next);
    next = token_next_not_ignorable_void(next);
    bool is_end = false;  // whether it is immediately after an IDENT
    size_t nest_counter = 0;
    // put the function arguments into a vector
    for (;;)
    {
      if (!is_end)
      {
        Vector *list = vector_new();
        vector_push(argument_real_list, list);
        while (nest_counter || next->kind != TK_RESERVED || next->len != 1 ||
               (next->str[0] != ',' && next->str[0] != ')'))
        {
          if (next->kind == TK_LINEBREAK)
          {
            line_count();
            token_void(next);
          }
          if (next->kind == TK_RESERVED && next->len == 1 &&
              next->str[0] == '(')
            nest_counter++;
          if (next->kind == TK_RESERVED && next->len == 1 &&
              next->str[0] == ')')
            nest_counter--;
          vector_push(list, next);
          next = token_next_not_ignorable(next);
        }
        is_end = true;
        continue;
      }
      if (is_end && next->kind == TK_RESERVED)
      {
        if (next->str[0] == ',')
        {
          next = next->next;
          is_end = false;
          continue;
        }
        if (next->str[0] == ')')
        {
          token_void(next);
          break;
        }
      }
      error_at(next->str, next->len, "Invalid #define directive.");
    }
    if (token_string && vector_size(token_string))
    {  // perform replacement for function-like macro

      // save the replaced data of the function-like macro
      Vector *result = vector_new();

      size_t directive_count = 0;
      for (size_t i = 1; i <= vector_size(token_string); i++)
      {
        Token *replace_token = vector_peek_at(token_string, i);
        bool is_args = false;
        bool is_ops = false;
        switch (replace_token->kind)
        {
          case TK_DIRECTIVE: directive_count++; continue;
          case TK_ILB: token_void(replace_token); continue;
          case TK_LINEBREAK: unreachable();
          default: break;
        }

        if (replace_token->len == 10 &&
            !strncmp(replace_token->str, "__VA_OPS__", 10))
          is_ops = true;
        else if (replace_token->len == 11 &&
                 !strncmp(replace_token->str, "__VA_ARGS__", 11))
        {
          switch (directive_count)
          {
            case 0: is_args = true; break;
            case 2:
            {
              is_ops = true;
              directive_count = 0;
              break;
            }
            default: unreachable();
          }
        }

        if (is_args || is_ops)
        {  // replace __VA_ARGS__, etc.
          // check that the last argument of the macro is "..."
          Token *last =
              vector_peek_at(argument_list, vector_size(argument_list));
          if (last->len != 3 || strncmp(last->str, "...", 3) ||
              (is_ops ? vector_size(argument_list) - 1 >
                            vector_size(argument_real_list)
                      : vector_size(argument_list) - 1 >=
                            vector_size(argument_real_list)))
            error_at(token->str, token->len,
                     "Invalid function-like macro argument.");
          if (is_ops &&
              vector_size(argument_list) - 1 ==
                  vector_size(argument_real_list) &&
              vector_size(result) >= 2)
          {
            Token *old_str = vector_peek_at(result, vector_size(result) - 1);
            size_t old_str_location = 1;
            while (old_str->kind == TK_IGNORABLE)
            {
              old_str = vector_peek_at(
                  result, vector_size(result) - ++old_str_location);
            }
            if (old_str->kind == TK_RESERVED && old_str->len == 1 &&
                old_str->str[0] == ',')
              vector_pop_at(result, vector_size(result) - old_str_location);
          }
          else
          {
            // start is the beginning of __VA_ARGS__
            Token *ptr = vector_peek_at(
                vector_peek_at(argument_real_list, vector_size(argument_list)),
                1);
            while (ptr != next)
            {  // next is the end of __VA_ARGS__
              vector_push(result, ptr);
              ptr = ptr->next;
            }
          }
        }
        else
        {
          bool is_argument = false;
          if (replace_token->kind == TK_IDENT ||
              replace_token->kind == TK_STRING ||
              replace_token->kind == TK_RESERVED)
          {  // perform normal replacement (other than __VA_ARGS__, etc.)
            for (size_t j = 1; j <= vector_size(argument_list); j++)
            {  // search for something that matches the macro argument
              Token *argument = vector_peek_at(argument_list, j);
              if (argument->len == replace_token->len &&
                  !strncmp(argument->str, replace_token->str, argument->len))
              {  // if it matches the macro argument
                size_t start = vector_size(result) + 1;
                for (size_t k = 1;
                     k <= vector_size(vector_peek_at(argument_real_list, j));
                     k++)
                {  // copy the actual argument corresponding to the macro
                   // argument
                  vector_push(
                      result,
                      vector_peek_at(vector_peek_at(argument_real_list, j), k));
                }
                is_argument = true;
                if (directive_count == 1)
                {  // stringizing
                  size_t stringizing_len_count = 0;
                  for (size_t k = start; k <= vector_size(result); k++)
                    stringizing_len_count +=
                        ((Token *)vector_peek_at(result, k))->len;

                  char *stringizing = malloc(stringizing_len_count + 2);
                  stringizing[0] = stringizing[stringizing_len_count + 1] = '"';
                  size_t stringizing_ptr = 1;
                  for (size_t k = start; k <= vector_size(result); k++)
                  {
                    Token *tmp = ((Token *)vector_pop_at(result, k));
                    strncpy(stringizing + stringizing_ptr, tmp->str, tmp->len);
                    stringizing_ptr += tmp->len;
                  }
                  Token *tmp = calloc(1, sizeof(Token));
                  tmp->kind = TK_STRING;
                  tmp->str = stringizing;
                  tmp->len = stringizing_len_count + 2;
                  vector_push(result, tmp);
                  directive_count = 0;
                }
                break;
              }
            }
          }
          if (!is_argument)
          {
            Token *tmp = malloc(sizeof(Token));
            memcpy(tmp, vector_peek_at(token_string, i), sizeof(Token));
            vector_push(result, tmp);
          }
        }
        if (replace_token->kind == TK_IDENT ||
            replace_token->kind == TK_STRING ||
            replace_token->kind == TK_RESERVED)
        {
          switch (directive_count)
          {
            case 0: break;
            case 2:
            {  // concatenation
              size_t vector_len = vector_size(result);
              Token *concatenation_last = vector_pop_at(result, vector_len);
              size_t concatenation_size = concatenation_last->len;
              size_t token_concatenation_start_location = 1;
              Token *tmp = vector_pop_at(result, vector_len - 1);
              while (tmp->kind == TK_IGNORABLE)
              {
                tmp = vector_pop_at(
                    result, vector_len - ++token_concatenation_start_location);
              }
              concatenation_size += tmp->len;
              char *concatenation_str = malloc(concatenation_size);
              strncpy(concatenation_str, tmp->str, tmp->len);
              strncpy(concatenation_str + tmp->len, concatenation_last->str,
                      concatenation_last->len);
              char *end = NULL;
              do
              {
                Token *concatenation_token =
                    tokenize_once(concatenation_str, &end);
                vector_push(result, concatenation_token);
              } while (end < concatenation_str + concatenation_size);
              directive_count = 0;
            }
            break;
            default: unreachable();
          }
        }
      }
      Token *ptr = old;
      for (size_t i = 1; i <= vector_size(result); i++)
      {
        Token *tmp = vector_peek_at(result, i);
        ptr = ptr->next = tmp;
      }
      ptr->next = next;
      for (size_t i = 1; i <= vector_size(result); i++)
        ident_replacement_recursive(vector_peek_at(result, i), hide_set);
      vector_free(result);
      return true;
    }
    token_void(token);
    return true;
  }
  return false;
}

// function to expand #define
// return value is the token at the end of the expansion
bool ident_replacement(Token *token)
{
  // expand what is defined by #define
  Vector *hide_set = vector_new();
  bool tmp = ident_replacement_recursive(token, hide_set);
  vector_free(hide_set);
  return tmp;
}

void undef_macro(Token *token)
{
  size_t location;
  int result = find_macro_name_without_hide_set(token, NULL, NULL, NULL, NULL,
                                                &location);
  switch (result)
  {
    case 0: break;
    case 1: vector_pop_at(object_like_macro_list, location); break;
    case 2: vector_pop_at(function_like_macro_list, location); break;
    default: unreachable(); break;
  }
}