// ------------------------------------------------------------------------------------
// preprocessor
// ------------------------------------------------------------------------------------

#include "include/preprocessor.h"
#include "include/tokenizer.h"
#include "include/file.h"
#include "include/define.h"
#include "include/debug.h"
#include "include/error.h"
#include <stdio.h>
#include <string.h>

Vector *Conditional_Inclusion_List;

void token_void(Token *token)
{
    token->kind = TK_IGNORABLE;
    token->str = NULL;
    token->len = 0;
}

// #~ のプリプロセッサで処理するものたち
void directive(Token *token)
{
    switch (token->len)
    {
    case 3:
        if (!strncmp(token->str, "#if", 3))
        {
            Vector *list = vector_shift(Conditional_Inclusion_List);
            if (token != vector_shift(list))
                unreachable();
        }
        break;
    case 5:
        if (!strncmp(token->str, "#line", 5))
        {
            return;
        }
        break;
    case 6:
        if (!strncmp(token->str, "#ifdef", 6))
        {

            return;
        }
        if (!strncmp(token->str, "#error", 6))
            error_at(token->str, token->len, "#error directive found");
        if (!strncmp(token->str, "#undef", 6))
        {
            return;
        }
        break;
    case 7:
        if (!strncmp(token->str, "#define", 7))
        {
            token_void(token);
            Token *ptr = token->next;
            while (ptr->kind == TK_IGNORABLE)
                ptr = ptr->next;

            if (ptr->kind != TK_IDENT)
                error_at(token->str, token->len, "invalid #define use");
            Vector *token_list = vector_new();
            Token *new = malloc(sizeof(Token));
            memcpy(new, ptr, sizeof(Token));
            vector_push(token_list, new);
            token_void(ptr);
            ptr = ptr->next;
            while (ptr->kind == TK_IGNORABLE)
                ptr = ptr->next;
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
            add_object_like_macro(token_list);
            return;
        }
        if (!strncmp(token->str, "#pragma", 7))
        {
            error_at(token->str, token->len, "not implemented");
            return;
        }
        break;
    case 8:
        if (!strncmp(token->str, "#include", 8))
        {
            return;
        }
        if (!strncmp(token->str, "#warning", 8))
        {
            return;
        }
        break;
    default:
        error_at(token->str, token->len, "unknown directive");
        break;
    }
}

void preprocessor()
{
    while (!at_eof())
    {
        Token *token = get_token();
        switch (token->kind)
        {
        case TK_DIRECTIVE: // '#'がトークン先頭に存在する場合
            directive(token);
            break;
        case TK_IDENT: // 識別子がトークン先頭の場合
        {
            Vector *hide_set = vector_new();
            for (;;)
            {
                Vector *token_string = NULL;
                Token *token_identifier;
                size_t is_defined = find_macro_name_without_hide_set(token, hide_set, &token_string, &token_identifier);
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
                else
                    break;
            }
        }
        break;
        default:
            break;
        }
        // 次のトークンに送る
        token = get_token();
        set_token(token->next);
    }
}

static FILE *fout;

static void writer(Token *token)
{
    for (;;)
    {
        if (token->kind == TK_EOF)
            break;
        fprintf(fout, "%.*s", (int)token->len, token->str);
        token = token->next;
    }
}

int main(int argc, char **argv)
{
    fprintf(stdout, "\e[32mpreprocessor\e[37m\n");
    if (argc != 3)
        error_exit("引数が正しくありません");
    char *input = openfile(argv[1]);
    // init
    error_init(argv[2], input);
    Conditional_Inclusion_List = vector_new();
    object_like_macro_list = vector_new();

    Token *token = tokenizer(input);
    // set_default_definition();
    preprocessor();
#ifdef DEBUG
    print_definition();
#endif
    fout = fopen(argv[2], "w");
    if (fout == NULL)
    {
        error_exit("ファイルに書き込めませんでした");
    }
    writer(token);
    return 0;
}