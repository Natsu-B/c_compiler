// ------------------------------------------------------------------------------------
// definition manager
// ------------------------------------------------------------------------------------

#include "include/define.h"
#include "include/tokenizer.h"
#include "include/vector.h"
#include "include/error.h"
#include <stdbool.h>
#include <string.h>

Vector *object_like_macro_list;

// 引数の文字列が object like macro に定義されている場合は1を
// function like macro に定義されている場合は2を、その他の場合は0を返す
// 1or2を返す場合は、token_stringにトークン列が入る
inline int find_macro_name_all(Token *identifier, Vector **token_string)
{
    return find_macro_name_without_hide_set(identifier, NULL, token_string, NULL);
}

// 引数の文字列が object like macro に定義されている場合は1を
// function like macro に定義されている場合は2を、その他の場合は0を返す
// 1or2を返す場合は、token_stringにトークン列が入る
// ただし、すでに展開済みのものを除く
int find_macro_name_without_hide_set(
    Token *identifier,
    Vector *hide_set,
    Vector **token_string,
    Token **token)
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
            *token_string = tmp->token_string;
            if (token)
                *token = tmp->identifier;
            return 1;
        }
    }
    return 0;
}

void add_object_like_macro(Vector *token_list)
{ // #define identifier token-string_opt
    Token *identifier = vector_shift(token_list);
    Vector *tmp;
    if (find_macro_name_all(identifier, &tmp))
        error_at(identifier->str, identifier->len, "identifier %s is already defined");
    object_like_macro_storage *new = malloc(sizeof(object_like_macro_storage));
    new->identifier = identifier;
    new->token_string = token_list;
    if (!object_like_macro_list)
        object_like_macro_list = vector_new();
    vector_push(object_like_macro_list, new);
}

void add_function_like_macro(Vector *token)
{
    unreachable();
}