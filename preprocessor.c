// ------------------------------------------------------------------------------------
// preprocessor
// ------------------------------------------------------------------------------------

#include "include/preprocessor.h"

#include <stdio.h>
#include <string.h>

#include "include/conditional_inclusion.h"
#include "include/debug.h"
#include "include/define.h"
#include "include/error.h"
#include "include/file.h"
#include "include/tokenizer.h"

Vector *Conditional_Inclusion_List;

char *File_Name;
size_t File_Line = 1;
char *File_Start;
long long include_level = -1;

void token_void(Token *token)
{
  token->kind = TK_IGNORABLE;
  token->str = NULL;
  token->len = 0;
}

Token *token_next_not_ignorable(Token *token)
{
  do
  {
    token = token->next;
  } while (token->kind == TK_IGNORABLE);
  return token;
}

Token *token_next_not_ignorable_void(Token *token)
{
  token = token->next;
  while (token->kind == TK_IGNORABLE)
  {
    token_void(token);
    token = token->next;
  }
  return token;
}

// File_Nameからディレクトリを得る
size_t get_current_directory_path()
{
  char *file_name = File_Name;
  char *last_path = NULL;
  while (*++file_name != '\0')  // NULL terminatorまで
    if (*file_name == '/')
      last_path = file_name;
  if (!last_path)
    return 0;
  return last_path + 1 - File_Name;
}

#ifdef __GNUC__
#define _TO_STRING(x) #x
#define TO_STRING(x) _TO_STRING(x)
static char gcc_lib_path[] =
    "/usr/lib/gcc/x86_64-linux-gnu/" TO_STRING(__GNUC__) "/include/";
static char local_include[] = "/usr/local/include/";
static char linux_include[] = "/usr/include/x86_64-linux-gnu/";
static char usr_include[] = "/usr/include/";
static char *lib_path[] = {
    gcc_lib_path,
    local_include,
    linux_include,
    usr_include,
};
static size_t lib_path_size[] = {
    sizeof(gcc_lib_path),
    sizeof(local_include),
    sizeof(linux_include),
    sizeof(usr_include),
};
#define MAX_LIB_PATH_SIZE sizeof(gcc_lib_path)
#else
// TODO
#endif

Token *preprocess(char *input, char *file_name, Token *token);

// #~ のプリプロセッサで処理するものたち
Token *directive(Token *token)
{
  switch (token->len)
  {
    case 1:
      if (token->str[0] == '#')
      {
        token_void(token);
        token = token_next_not_ignorable_void(token);
        char *directive_name = malloc(token->len + 1);
        strncpy(directive_name + 1, token->str, token->len);
        directive_name[0] = '#';
        token->str = directive_name;
        token->len++;
        return directive(token);
      }
      break;
    case 3:
      if (!strncmp(token->str, "#if", 3))
      {
        Vector *conditional_list;
        for (;;)
        {
          if (!vector_has_data(Conditional_Inclusion_List))
            unreachable();
          conditional_list = vector_shift(Conditional_Inclusion_List);
          if (token == vector_peek_at(conditional_list, 1))
            break;
        }
        conditional_inclusion(token_if, conditional_list);
        return token->next;
      }
      break;
    case 5:
      if (!strncmp(token->str, "#line", 5))
      {
        unimplemented();
        return token->next;
      }
      break;
    case 6:
      if (!strncmp(token->str, "#ifdef", 6))
      {
        Vector *conditional_list;
        for (;;)
        {
          if (!vector_has_data(Conditional_Inclusion_List))
            unreachable();
          conditional_list = vector_shift(Conditional_Inclusion_List);
          if (token == vector_peek_at(conditional_list, 1))
            break;
        }
        conditional_inclusion(token_ifdef, conditional_list);
        return token->next;
      }
      if (!strncmp(token->str, "#error", 6))
        error_at(token->str, token->len, "#error directive found");
      if (!strncmp(token->str, "#undef", 6))
      {
        unimplemented();
        return token->next;
      }
      break;
    case 7:
      if (!strncmp(token->str, "#define", 7))
      {
        token_void(token);
        Token *ptr = token_next_not_ignorable_void(token);

        Vector *token_list = vector_new();
        Token *new = malloc(sizeof(Token));
        memcpy(new, ptr, sizeof(Token));
        vector_push(token_list, new);
        token_void(ptr);
        ptr = token_next_not_ignorable_void(ptr);

        bool is_function_like = false;
        switch (ptr->kind)
        {
          case TK_LINEBREAK:
          case TK_IDENT: break;
          case TK_RESERVED:
            // function like macroのときの動作
            if (ptr->str[0] == '(')
            {
              is_function_like = true;
              Vector *formal_parameter = vector_new();
              vector_push(token_list, formal_parameter);
              token_void(ptr);
              for (;;)
              {
                ptr = token_next_not_ignorable_void(ptr);
                if (ptr->kind == TK_IDENT)
                {
                  Token *new = malloc(sizeof(Token));
                  memcpy(new, ptr, sizeof(Token));
                  token_void(ptr);
                  for (size_t i = 1; i <= vector_size(formal_parameter); i++)
                  {
                    Token *argument = vector_peek_at(formal_parameter, i);
                    if (argument->len == new->len &&
                        !strncmp(argument->str, new->str, argument->len))
                      error_at(new->str, new->len, "duplicate macro parameter");
                  }
                  vector_push(formal_parameter, new);
                  while (ptr->kind == TK_IGNORABLE)
                    ptr = ptr->next;
                  if (ptr->kind == TK_RESERVED)
                  {
                    if (ptr->str[0] == ')')
                    {
                      token_void(ptr);
                      ptr = token_next_not_ignorable_void(ptr);
                      break;
                    }
                    if (ptr->str[0] == ',')
                    {
                      token_void(ptr);
                      continue;
                    }
                  }
                }
                error_at(token->str, token->len, "Invalid #define use");
              }
              break;
            }
          // fall through
          default: error_at(token->str, token->len, "invalid #define use");
        }
        for (;;)
        {
          if (ptr->kind == TK_LINEBREAK)
            break;

          Token *new = malloc(sizeof(Token));
          memcpy(new, ptr, sizeof(Token));
          vector_push(token_list, new);
          token_void(ptr);

          ptr = ptr->next;
        }
        if (is_function_like)
          add_function_like_macro(token_list);
        else
          add_object_like_macro(token_list);
        return token->next;
      }
      if (!strncmp(token->str, "#ifndef", 7))
      {
        Vector *conditional_list = vector_shift(Conditional_Inclusion_List);
        if (token != vector_peek_at(conditional_list, 1))
          unreachable();
        conditional_inclusion(token_ifndef, conditional_list);
        return token->next;
      }
      if (!strncmp(token->str, "#pragma", 7))
      {
        unimplemented();
        return token->next;
      }
      break;
    case 8:
      if (!strncmp(token->str, "#include", 8))
      {
        token_void(token);
        Token *tmp = token_next_not_ignorable(token);
        while (tmp->kind != TK_LINEBREAK)
        {
          ident_replacement(tmp);
          tmp = token_next_not_ignorable(tmp);
        }
        char *file_name = NULL;  // #include "ident"のident
        size_t file_len = 0;
        size_t directory_path_size = get_current_directory_path();
        token = token_next_not_ignorable_void(token);
        if (token->kind == TK_RESERVED && token->str[0] == '<')
        {  // #include < ident >
          token_void(token);
          token = token->next;
          char *include_file_start = token->str;
          Token *include_file_start_token = token;
          while (token->kind != TK_RESERVED || token->str[0] != '>')
          {
            if (token->kind == TK_LINEBREAK)
              error_at(token->str, token->len, "Invalid #include path");
            file_len += token->len;
            token = token->next;
          }
          file_name = malloc(file_len + 1 /* '\0' */ + MAX_LIB_PATH_SIZE);
          memcpy(file_name, include_file_start, file_len);
          while (token != include_file_start_token)
          {
            token_void(include_file_start_token);
            include_file_start_token = include_file_start_token->next;
          }
        }
        else if (token->kind == TK_STRING)
        {  // #include " ident "
          file_len = token->len - 2;
          file_name = malloc(file_len + 1 /* '\0' */ + MAX_LIB_PATH_SIZE);
          memcpy(file_name, token->str + 1, token->len - 2);
        }
        else
          error_at(token->str, token->len, "invalid #include directive");
        file_name[file_len] = '\0';
        FILE *include_file_ptr = NULL;
        if (directory_path_size)
        {
          char *file_name_with_dir = malloc(file_len + 1 + directory_path_size);
          memcpy(file_name_with_dir, File_Name, directory_path_size);
          memcpy(file_name_with_dir + directory_path_size, file_name,
                 file_len + 1);
          include_file_ptr = fopen(file_name_with_dir, "r");
          if (include_file_ptr)
            file_name = file_name_with_dir;
        }
        else
          include_file_ptr = fopen(file_name, "r");
        if (!include_file_ptr)
        {  // stdlib を読み込む
          for (size_t i = 0; i < sizeof(lib_path) / sizeof(char *); i++)
          {
            memmove(file_name + lib_path_size[i] - 1,
                    file_name + (i ? lib_path_size[i - 1] - 1 : 0),
                    file_len + 1);
            memcpy(file_name, lib_path[i], lib_path_size[i] - 1);
            pr_debug("file path: %s", file_name);
            include_file_ptr = fopen(file_name, "r");
            if (include_file_ptr)
              break;
          }
        }
        if (!include_file_ptr)
          error_at(token->str - file_len - 2, file_len, "file not found");
        Token *old = token;
        token_void(old);
        token = token_next_not_ignorable_void(token);
        if (token->kind != TK_LINEBREAK)
          error_at(token->str, token->len, "invalid #include directive");
        File_Line++;
        token_void(token);
        preprocess(file_read(include_file_ptr), file_name, old);
        return token;
      }
      if (!strncmp(token->str, "#warning", 8))
      {
        unimplemented();
        return token->next;
      }
      break;
    default: break;
  }
  error_at(token->str, token->len, "unknown directive");
  return NULL;  // unreachable
}

void preprocessor()
{
  pr_debug2("start preprocessor");
  Token *token = get_token();
  while (token->kind != TK_EOF)
  {
    switch (token->kind)
    {
      case TK_DIRECTIVE:  // '#'がトークン先頭に存在する場合
        token = directive(token);
        break;
      case TK_LINEBREAK: File_Line++; break;
      case TK_IDENT:  // 識別子がトークン先頭の場合
        ident_replacement(token);
        break;
      default: break;
    }
    // 次のトークンに送る
    token = token->next;
  }
}

Token *preprocess(char *input, char *file_name, Token *token)
{
  pr_debug("start preprocessing %s", file_name);
  char *old_file_name = File_Name;
  size_t old_file_line = File_Line;
  char *old_file_start = File_Start;
  Vector *old_conditional_inclusion_list = Conditional_Inclusion_List;
  Conditional_Inclusion_List = vector_new();
  Token *next_token = token ? token->next : NULL;
  File_Name = file_name;
  File_Line = 1;
  File_Start = input;
  error_init(File_Name, input);
  Token *token_first = tokenizer(input, next_token);
  if (token)
    token->next = token_first;
  else
    token = token_first;
  include_level++;
  preprocessor();
#ifdef DEBUG
  print_definition();
#endif
  include_level--;
  vector_free(Conditional_Inclusion_List);
  Conditional_Inclusion_List = old_conditional_inclusion_list;
  File_Name = old_file_name;
  File_Line = old_file_line;
  File_Start = old_file_start;
  if (File_Name)
    error_init(File_Name, File_Start);
  pr_debug("end preprocessing %s", file_name);
  return token;
}

[[noreturn]]
void preprocessed_file_writer(Token *token, char *output_filename)
{
  FILE *fout = fopen(output_filename, "w");
  if (fout == NULL)
  {
    error_exit("ファイルに書き込めませんでした");
  }
  for (;;)
  {
    if (!token)
      break;
    fprintf(fout, "%.*s", (int)token->len, token->str);
    token = token->next;
  }
  exit(0);
}

void init_preprocessor()
{
  Conditional_Inclusion_List = vector_new();
  object_like_macro_list = vector_new();
  // set_default_definition();
}