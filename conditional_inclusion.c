#include "include/conditional_inclusion.h"
#include "include/preprocessor.h"
#include "include/define.h"
#include "include/vector.h"
#include "include/error.h"
#include <string.h>

// 引数のheadトークンから nextトークンまでのトークンを消す
// ただし、TK_LINEBREAKは残す
static void clean_while_next(Token *head, Token *next)
{
    while (head != next)
    {
        if (head->kind == TK_EOF)
            error_exit("#ifディレクティブが閉じていません");
        if (head->kind != TK_LINEBREAK)
            token_void(head);
        head = head->next;
    }
}

// #if #ifdef #ifndefに対応する #else #elif #endif 等を処理する関数
void next_conditional_inclusion(Token *token, bool is_true, Vector *conditional_list)
{
    switch (token->len)
    {
    case 5: // #else #elif
        if (!strncmp(token->str, "#else", 5))
        { // #else
            token_void(token);
            Token *next = vector_shift(conditional_list);
            if (is_true)
                clean_while_next(token, next);
            next_conditional_inclusion(next, !is_true, conditional_list);
        }
        else
        { // #elif
            unimplemented();
        }
        break;
    case 6: // #endif
        token_void(token);
        return;
    case 8: // #elifdef
        unimplemented();
        break;
    case 9: // #elifndef
        unimplemented();
        break;
    default:
        unreachable();
        break;
    }
}

void conditional_inclusion(if_directive type, Vector *conditional_list)
{
    switch (type)
    {
    case token_if:
        unimplemented();
        break;
    case token_ifdef:
    case token_ifndef:
        Token *head = vector_shift(conditional_list);
        token_void(head);
        do
        {
            head = head->next;
        } while (head->kind == TK_IGNORABLE);
        bool is_true = false;
        if (find_macro_name_all(head))
        {
            if (type == token_ifdef)
                is_true = true;
        }
        else if (type == token_ifndef)
            is_true = true;
        token_void(head);
        head = head->next;

        // #else #endif 等の対応するディレクティブ
        Token *next = vector_shift(conditional_list);
        if (!is_true)
            clean_while_next(head, next);
        next_conditional_inclusion(next, is_true, conditional_list);
        break;
    default:
        unreachable();
    }
}