// ------------------------------------------------------------------------------------
// tokenizer
// ------------------------------------------------------------------------------------

#include "include/tokenizer.h"
#include "include/error.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

Token *token; // トークンの実体

// 次のトークンが引数の記号だったら読み進めtrueをその他のときはfalseを返す関数
bool consume(char *op)
{
    pr_debug2("consume %*s", token->len, op);
    if (token->kind != TK_RESERVED ||
        strlen(op) != token->len ||
        memcmp(op, token->str, token->len))
        return false;

    token = token->next;
    return true;
}

// 次のトークンが引数の記号だったら読み進め、そうでなければerror_atを呼び出す
void expect(char *op)
{
    if (!consume(op))
        error_at(token->str, "トークンが %*s でありませんでした", token->len, op);
}

// 次のトークンが整数だった場合読み進め、それ以外だったらエラーを返す関数
long expect_number()
{
    if (token->kind != TK_NUM)
        error_at(token->str, "トークンが整数でありませんでした");
    long val = token->val;
    token = token->next;
    return val;
}

// トークンが最後(TK_EOF)だったらtrueを、でなければfalseを返す関数
bool at_eof()
{
    return token->kind == TK_EOF;
}

// 新しくトークンを作る関数
Token *new_token(TokenKind kind, Token *old, char *str)
{
    Token *new = calloc(1, sizeof(Token));
    new->kind = kind;
    new->str = str;
    old->next = new;

    return new;
}

// トークナイズする関数
void tokenizer(char *input)
{
    pr_debug("start tokenizer...");
    Token head;
    head.next = NULL;
    Token *cur = &head;

    while (*input)
    {
        if (isspace(*input))
        {
            input++;
            continue;
        }

        if (strchr("+-*/()=!<>", *input))
        {
            cur = new_token(TK_RESERVED, cur, input);
            // "==", "<=", ">=", "!=" の場合
            if (*(input + 1) == '=')
            {
                pr_debug2("find RESERVED token: %2s", input);
                cur->len = 2;
                input += 2;
            }
            else
            {
                pr_debug2("find RESERVED token: %1s", input);
                cur->len = 1;
                input++;
            }
            continue;
        }

        if (isdigit(*input))
        {
            cur = new_token(TK_NUM, cur, input);
            cur->val = strtol(input, &input, 10);
            pr_debug2("find NUM token: %ld", cur->val);
            continue;
        }

        // 改行は飛ばす
        if (*input == '\n')
        {
            input++;
            continue;
        }
        error_at(input, "トークナイズに失敗しました");
    }

    new_token(TK_EOF, cur, input);
    pr_debug("complite tokenize");
    token = head.next;
}
