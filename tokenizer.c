// ------------------------------------------------------------------------------------
// tokenizer
// ------------------------------------------------------------------------------------

#include "include/tokenizer.h"
#include "include/error.h"
#include "include/debug.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

Token *token; // トークンの実体

// 次の次のトークン(1つ先のトークン)が引数のトークンだったらtrueを変える
bool peek_next_TokenKind(TokenKind kind)
{
    if (token->next->kind != kind)
        return false;
    return true;
}

bool peek_next(char *op)
{
    if (token->next->len == strlen(op) &&
        !strncmp(token->next->str, op, token->next->len))
        return true;
    return false;
}

// 次のトークンが引数のトークンの種類だったら読み勧めてそのTokenを返す
Token *consume_TokenKind(TokenKind kind)
{
    if (token->kind != kind)
        return NULL;
    Token *ret = token;
    token = token->next;
    return ret;
}

// 次のトークンが引数の記号だったら読み進めtrueをその他のときはfalseを返す関数
bool consume(char *op)
{
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
        error_at(token->str, "トークンが %.*s でありませんでした", token->len, op);
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

// 与えられた引数がトークンを構成するかどうか 英数字と '_'
int is_alnum(char c)
{
    return ('a' <= c && c <= 'z') ||
           ('A' <= c && c <= 'Z') ||
           ('0' <= c && c <= '9') ||
           (c == '_');
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

        if (strchr("+-*/()=!<>;", *input))
        {
            cur = new_token(TK_RESERVED, cur, input);
            // "==", "<=", ">=", "!=" の場合
            if (*(input + 1) == '=')
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
            continue;
        }

        if (isdigit(*input))
        {
            cur = new_token(TK_NUM, cur, input);
            cur->val = strtol(input, &input, 10);
            pr_debug2("find NUM token: %ld", cur->val);
            continue;
        }

        // 英数字と'_'のみの場合は変数または予約語とみなす
        int i = 0;
        while (is_alnum(*input))
        {
            i++;
            input++;
        }
        if (i)
        {
            // if 文か否かを判別する
            if (i == 2) // strlen("if") = 2
            {
                if (!strncmp(input - i, "if", i))
                {
                    cur = new_token(TK_IF, cur, input - i);
                    cur->len = i;
                    continue;
                }
            }

            // else 文か否かを判別する
            if (i == 4) // strlen("else") = 4
            {
                if (!strncmp(input - i, "else", i))
                {
                    cur = new_token(TK_ELSE, cur, input - i);
                    cur->len = i;
                    continue;
                }
            }

            // for 文か否かを判別する
            if (i == 3) // strlen("for") = 3
            {
                if (!strncmp(input - i, "for", i))
                {
                    cur = new_token(TK_FOR, cur, input - i);
                    cur->len = i;
                    continue;
                }
            }

            // while文か否かを判別する
            if (i == 5) // strlen("while") = 5
            {
                if (!strncmp(input - i, "while", i))
                {
                    cur = new_token(TK_WHILE, cur, input - i);
                    cur->len = i;
                    continue;
                }
            }

            // return 文か否かを判別する
            if (i == 6) // strlen("return") = 6
            {
                if (!strncmp(input - i, "return", i))
                {
                    cur = new_token(TK_RETURN, cur, input - i);
                    cur->val = i;
                    continue;
                }
            }
            cur = new_token(TK_IDENT, cur, input - i);
            cur->val = i;
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
#ifdef DEBUG
    char *TokenKindList[TK_END] = {TokenKindTable};
    print_tokenize_result(head.next, TokenKindList);
#endif
    token = head.next;
}
