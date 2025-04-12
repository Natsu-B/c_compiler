// ------------------------------------------------------------------------------------
// preprocessor
// ------------------------------------------------------------------------------------

#include "include/preprocessor.h"
#include "include/conditional_inclusion.h"
#include "include/tokenizer.h"
#include "include/file.h"
#include "include/define.h"
#include "include/debug.h"
#include "include/error.h"
#include <stdio.h>
#include <string.h>

Vector *Conditional_Inclusion_List;

char *File_Name;
size_t File_Line;
char *File_Start;

typedef struct
{
    char *file_name;
    size_t file_line;
} file_list;

void token_void(Token *token)
{
    token->kind = TK_IGNORABLE;
    token->str = NULL;
    token->len = 0;
}

#ifdef __GNUC__
#define _TO_STRING(x) #x
#define TO_STRING(x) _TO_STRING(x)
static char gcc_lib_path[] = "/usr/lib/gcc/x86_64-linux-gnu/" TO_STRING(__GNUC__) "/include/";
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
    case 3:
        if (!strncmp(token->str, "#if", 3))
        {
            Vector *conditional_list = vector_shift(Conditional_Inclusion_List);
            if (token != vector_peek_at(conditional_list, 1))
                unreachable();
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
            Vector *conditional_list = vector_shift(Conditional_Inclusion_List);
            if (token != vector_peek_at(conditional_list, 1))
                unreachable();
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
            Token *ptr = token->next;
            while (ptr->kind == TK_IGNORABLE)
                ptr = ptr->next;

            Vector *token_list = vector_new();
            Token *new = malloc(sizeof(Token));
            memcpy(new, ptr, sizeof(Token));
            vector_push(token_list, new);
            token_void(ptr);
            ptr = ptr->next;
            while (ptr->kind == TK_IGNORABLE)
                ptr = ptr->next;
            bool is_function_like = false;
            switch (ptr->kind)
            {
            case TK_LINEBREAK:
            case TK_IDENT:
                break;
            case TK_RESERVED:
                // function like macroのときの動作
                if (ptr->str[0] == '(')
                {
                    is_function_like = true;
                    Vector *formal_parameter = vector_new();
                    vector_push(token_list, formal_parameter);
                    token_void(ptr);
                    ptr = ptr->next;
                    for (;;)
                    {
                        while (ptr->kind == TK_IGNORABLE)
                            ptr = ptr->next;
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
                                    ptr = ptr->next;
                                    break;
                                }
                                if (ptr->str[0] == ',')
                                {
                                    token_void(ptr);
                                    ptr = ptr->next;
                                    continue;
                                }
                            }
                        }
                        error_at(token->str, token->len, "Invalid #define use");
                    }
                    break;
                }
            // fall through
            default:
                error_at(token->str, token->len, "invalid #define use");
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
            set_token(ptr);
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
            char *file_name = NULL;
            size_t file_len = 0;
            while (token->kind == TK_IGNORABLE)
                token = token->next;
            if (token->kind == TK_RESERVED &&
                token->str[0] == '<')
            { // #include < ident >
                token_void(token);
                token = token->next;
                char *include_file_start = token->str;
                Token *include_file_start_token = token;
                while (token->kind != TK_RESERVED ||
                       token->str[0] != '>')
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
            { // #include " ident "
                file_len = token->len - 2;
                file_name = malloc(file_len + 1 /* '\0' */ + MAX_LIB_PATH_SIZE);
                memcpy(file_name, token->str + 1, token->len - 2);
            }
            file_name[file_len] = '\0';
            FILE *include_file_ptr = fopen(file_name, "r");
            if (!include_file_ptr)
            { // stdlib を読み込む
                for (size_t i = 0; i < sizeof(lib_path) / sizeof(char *); i++)
                {
                    memmove(file_name + lib_path_size[i] - 1, file_name + (i ? lib_path_size[i - 1] - 1 : 0), file_len + 1);
                    memcpy(file_name, lib_path[i], lib_path_size[i] - 1);
                    pr_debug("file path: %s", file_name);
                    include_file_ptr = fopen(file_name, "r");
                    if (include_file_ptr)
                        break;
                }
            }
            if (!include_file_ptr)
                error_at(token->str - file_len - 2, file_len, "file not found");
            token_void(token);
            Token *next = token->next;
            preprocess(file_read(include_file_ptr), file_name, token);
            return next;
        }
        if (!strncmp(token->str, "#warning", 8))
        {
            unimplemented();
            return token->next;
        }
        break;
    default:
        break;
    }
    error_at(token->str, token->len, "unknown directive");
    return NULL; // unreachable
}

void preprocessor()
{
    Token *token = get_token();
    while (token->kind != TK_EOF)
    {
        switch (token->kind)
        {
        case TK_DIRECTIVE: // '#'がトークン先頭に存在する場合
            token = directive(token);
            break;
        case TK_IDENT: // 識別子がトークン先頭の場合
        {
            // #defineで定義されているものを展開する
            Vector *hide_set = vector_new();
            for (;;)
            {
                Vector *token_string = NULL;
                Token *token_identifier;
                Vector *argument_list = NULL;
                size_t is_defined =
                    find_macro_name_without_hide_set(
                        token,
                        hide_set,
                        &argument_list,
                        &token_string,
                        &token_identifier);
                if (is_defined == 1)
                { // object like macro の場合
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
                    }
                    else
                    {
                        token_void(token);
                        break;
                    }
                }
                else if (is_defined == 2 &&
                         token->next->kind == TK_RESERVED &&
                         token->next->str[0] == '(')
                { // function like macro の場合
                    token_void(token);
                    token_void(token->next);
                    Vector *argument_real_list = vector_new();
                    vector_push(hide_set, token_identifier);
                    Token *tmp = token;
                    Token *old = token;
                    Token *next = token->next;
                    next = next->next;
                    bool is_end = false; // IDENTの直後かどうか
                    for (;;)
                    { // TODO '('のネストが無視される
                        if (next->kind == TK_IGNORABLE)
                        {
                            token_void(next);
                            next = next->next;
                            continue;
                        }
                        if (!is_end && next->kind == TK_IDENT)
                        {
                            Token *new = malloc(sizeof(Token));
                            memcpy(new, next, sizeof(Token));
                            token_void(next);
                            vector_push(argument_real_list, new);
                            next = next->next;
                            is_end = true;
                            continue;
                        }
                        if (is_end && next->kind == TK_RESERVED)
                        {
                            if (next->str[0] == ',')
                            {
                                token_void(next);
                                next = next->next;
                                is_end = false;
                                continue;
                            }
                            if (next->str[0] == ')')
                            {
                                token_void(next);
                                next = next->next;
                                break;
                            }
                        }
                        error_at(next->str, next->len, "Invalid #define directive");
                    }
                    if (token_string && vector_size(token_string))
                    {
                        for (size_t i = 1; i <= vector_size(token_string); i++)
                        {
                            bool is_argument = false;
                            Token *replace_token = vector_peek_at(token_string, i);
                            for (size_t j = 1; j <= vector_size(argument_list); j++)
                            {
                                Token *argument = vector_peek_at(argument_list, j);
                                if (argument->len == replace_token->len &&
                                    !strncmp(argument->str, replace_token->str, argument->len))
                                {
                                    memcpy(tmp, vector_peek_at(argument_real_list, j), sizeof(Token));
                                    is_argument = true;
                                }
                            }
                            if (!is_argument)
                                memcpy(tmp, vector_peek_at(token_string, i), sizeof(Token));
                            tmp->next = malloc(sizeof(Token));
                            old = tmp;
                            tmp = tmp->next;
                        }
                        old->next = next;
                    }
                    else
                    {
                        token_void(token);
                        break;
                    }
                }
                else
                    break;
            }
            vector_free(hide_set);
        }
        break;
        default:
            break;
        }
        // 次のトークンに送る
        token = token->next;
    }
}

Token *preprocess(char *input, char *file_name, Token *token)
{
    pr_debug("start preprocessing %s", file_name);
    file_list *file = malloc(sizeof(file_list));
    file->file_name = file_name;
    file->file_line = 0;
    char *old_file_name = File_Name;
    size_t old_file_line = File_Line;
    char *old_file_start = File_Start;
    Vector *old_conditional_inclusion_list = Conditional_Inclusion_List;
    Conditional_Inclusion_List = vector_new();
    Token *next_token = token ? token->next : NULL;
    File_Name = file_name;
    File_Line = 0;
    File_Start = input;
    error_init(File_Name, input);
    Token *token_first = tokenizer(input, next_token);
    if (token)
        token->next = token_first;
    else
        token = token_first;
    preprocessor();
#ifdef DEBUG
    print_definition();
#endif
    vector_free(Conditional_Inclusion_List);
    Conditional_Inclusion_List = old_conditional_inclusion_list;
    File_Name = old_file_name;
    File_Line = old_file_line;
    File_Start = old_file_start;
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