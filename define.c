// ------------------------------------------------------------------------------------
// definition manager
// ------------------------------------------------------------------------------------

#include "include/define.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

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

// 引数の文字列が object like macro に定義されている場合は1を
// function like macro に定義されている場合は2を、その他の場合は0を返す
inline int find_macro_name_all(Token *identifier)
{
  return find_macro_name_without_hide_set(identifier, NULL, NULL, NULL, NULL,
                                          NULL);
}

// 引数の文字列が object like macro に定義されている場合は1を
// function like macro に定義されている場合は2を、その他の場合は0を返す
// 1or2を返す場合は、token_stringにトークン列が、tokenにmacroの名前が、
// locationにmacroが(object or function)_macro_listのどの位置にあるか
// が入る(それぞれがnonnullの場合)
// 2を返す場合はargument_listに関数引数列が入る
// ただし、すでに展開済みのものを除く
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
             "identifier %s is already defined");
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
             "identifier %s is already defined");

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
      char *file_name = malloc(file_name_len + 2);  // NULL terminatorは必要ない
      strncpy(file_name + 1, File_Name, file_name_len);
      file_name[0] = file_name[file_name_len + 1] = '"';
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
          error_exit("failed to preprocess __LINE__");
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
        error_exit("failed to get time");
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
          error_exit("failed to preprocess __INCLUDE_LEVEL__");
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
  {  // object like macro の場合
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
      ident_replacement_recursive(next, hide_set);
      return true;
    }
    token_void(token);
    return true;
  }
  else if (is_defined == 2 &&
           token_next_not_ignorable(token)->kind == TK_RESERVED &&
           token_next_not_ignorable(token)->str[0] == '(')
  {  // function like macro の場合
    token_void(token);
    // function like macroの実際の引数リスト Vectorが入る
    Vector *argument_real_list = vector_new();
    vector_push(hide_set, token_identifier);
    Token *old = token;
    Token *next = token_next_not_ignorable_void(token);
    token_void(next);
    next = token_next_not_ignorable_void(token);
    bool is_end = false;  // IDENTの直後かどうか
    size_t nest_counter = 0;
    // 関数の引数をvectorに入れる
    for (;;)
    {
      if (next->kind == TK_IGNORABLE || next->kind == TK_ILB)
      {
        if (next->kind == TK_ILB)
          line_count();
        next = next->next;
        continue;
      }
      if (!is_end)
      {
        Vector *list = vector_new();
        vector_push(argument_real_list, list);
        while (nest_counter || next->kind != TK_RESERVED ||
               (next->str[0] != ',' && next->str[0] != ')'))
        {
          if (next->kind == TK_LINEBREAK)
            error_at(next->str, next->len, "invalid define directive");
          if (next->kind == TK_RESERVED && next->str[0] == '(')
            nest_counter++;
          if (next->kind == TK_RESERVED && next->str[0] == ')')
            nest_counter--;
          vector_push(list, next);
          next = next->next;
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
      error_at(next->str, next->len, "Invalid #define directive");
    }
    Token *tmp = old->next = malloc(sizeof(Token));
    Token *old_str = NULL;
    if (token_string && vector_size(token_string))
    {  // function like macroの置換をしていく
      size_t directive_count = 0;
      for (size_t i = 1; i <= vector_size(token_string); i++)
      {
        Token *replace_token = vector_peek_at(token_string, i);
        bool is_args = false;
        bool is_ops = false;
        switch (replace_token->kind)
        {
          case TK_DIRECTIVE: directive_count++; continue;
          case TK_ILB: line_count(); continue;
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
        {  // __VA_ARGS__ 等を置換する
          // macroの引数の最後が"..."であることを確認する
          Token *last =
              vector_peek_at(argument_list, vector_size(argument_list));
          if (last->len != 3 || strncmp(last->str, "...", 3) ||
              (is_ops ? vector_size(argument_list) - 1 >
                            vector_size(argument_real_list)
                      : vector_size(argument_list) - 1 >=
                            vector_size(argument_real_list)))
            error_at(token->str, token->len,
                     "invalid function like macro argument");
          if (is_ops &&
              vector_size(argument_list) - 1 ==
                  vector_size(argument_real_list) &&
              old_str->len == 1 && old_str->str[0] == ',')
            token_void(old_str);
          else
          {
            // startは__VA_ARGS__の最初
            Token *ptr = vector_peek_at(
                vector_peek_at(argument_real_list, vector_size(argument_list)),
                1);
            while (ptr != next)
            {  // nextは__VA_ARGS__の終了部分
              memcpy(tmp, ptr, sizeof(Token));
              old_str = tmp;
              tmp = tmp->next = malloc(sizeof(Token));
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
          {  // 通常の置換を行っていく (__VA_ARGS__等以外の)
            for (size_t j = 1; j <= vector_size(argument_list); j++)
            {  // マクロの引数と同じものがないか探す
              Token *argument = vector_peek_at(argument_list, j);
              if (argument->len == replace_token->len &&
                  !strncmp(argument->str, replace_token->str, argument->len))
              {  // マクロの引数と同じ場合
                Token *start = tmp;
                for (size_t k = 1;
                     k <= vector_size(vector_peek_at(argument_real_list, j));
                     k++)
                {  // マクロの引数に対応する実引数をコピーしてくる
                  memcpy(
                      tmp,
                      vector_peek_at(vector_peek_at(argument_real_list, j), k),
                      sizeof(Token));
                  old_str = tmp;
                  tmp = tmp->next = malloc(sizeof(Token));
                }
                token_void(tmp);
                is_argument = true;
                if (directive_count == 1)
                {  // stringizing
                  size_t stringizing_len_count = 0;
                  Token *ptr = start;
                  while (ptr != tmp)
                  {
                    stringizing_len_count += ptr->len;
                    ptr = ptr->next;
                  }
                  char *stringizing = malloc(stringizing_len_count + 2);
                  stringizing[0] = stringizing[stringizing_len_count + 1] = '"';
                  size_t stringizing_ptr = 1;
                  ptr = start;
                  while (ptr != tmp)
                  {
                    strncpy(stringizing + stringizing_ptr, ptr->str, ptr->len);
                    stringizing_ptr += ptr->len;
                    token_void(ptr);
                    ptr = ptr->next;
                  }
                  directive_count = 0;
                  start->kind = TK_STRING;
                  start->str = stringizing;
                  start->len = stringizing_len_count + 2;
                  start->next = tmp;
                }
                ident_replacement_recursive(start, hide_set);
                break;
              }
            }

            switch (directive_count)
            {
              case 0: break;
              case 2:
              {
                if (!old_str)
                  unreachable();
                // concatenation
                size_t concatenation_size = old_str->len + tmp->len + 1;
                char *concatenation_str = malloc(concatenation_size);
                int print_size = sprintf(concatenation_str, "%.*s",
                                         (int)old_str->len, old_str->str);
                if (print_size != (int)old_str->len)
                  unreachable();
                print_size = sprintf(concatenation_str + old_str->len, "%.*s",
                                     (int)tmp->len, tmp->str);
                if (print_size != (int)tmp->len)
                  unreachable();
                char *end = NULL;
                Token *concatenation_token =
                    tokenize_once(concatenation_str, &end);
                if (end != concatenation_str + concatenation_size - 1)
                  unreachable();
                old_str->kind = concatenation_token->kind;
                old_str->len = concatenation_token->len;
                old_str->str = concatenation_token->str;
                tmp = old_str;
                directive_count = 0;
              }
              break;
              default: unreachable();
            }
          }
          if (!is_argument)
            memcpy(tmp, vector_peek_at(token_string, i), sizeof(Token));
        }
        if (directive_count)
          unreachable();
        directive_count = 0;
        old = tmp;
        tmp = tmp->next = malloc(sizeof(Token));
      }
      old->next = next;
      return true;
    }
    token_void(token);
    return true;
  }
  return false;
}

// #define を展開する関数
// 返り値は展開終了のtoken
bool ident_replacement(Token *token)
{
  // #defineで定義されているものを展開する
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