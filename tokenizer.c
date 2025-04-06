// ------------------------------------------------------------------------------------
// tokenizer
// ------------------------------------------------------------------------------------
#include "include/tokenizer.h"
#include "include/vector.h"
#include "include/error.h"
#include "include/debug.h"

#ifdef PREPROCESS
#include "include/preprocessor.h"
static Vector *nest_list; // nestごとにConditional Inclusion Listを分けて持つ
#endif

#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

Token *token;     // トークンの実体
Token *token_old; // tokenの一つあとのトークン

const char *tokenkindlist[TK_END] = {TokenKindTable};

// トークンの位置を引数の位置に変更する
// かなり危険なため利用は注意を
void set_token(Token *next)
{
    token = next;
}

Token *get_token()
{
    return token;
}

// 次の次のトークン(1つ先のトークン)が引数のトークンだったらtrueを返す
bool peek_next_TokenKind(TokenKind kind)
{
    if (token->next->kind != kind)
        return false;
    return true;
}

bool peek_next(char *op)
{
    if (token->next->kind == TK_RESERVED &&
        token->next->len == strlen(op) &&
        !strncmp(token->next->str, op, token->next->len))
        return true;
    return false;
}

// 次のトークンが引数のkindのトークンで、その次のトークンがTK_RESERVEDで
// 引数のreservedと等しければ消費する その他の場合NULLを返す
Token *consume_token_if_next_matches(TokenKind kind, char reserved)
{
    if (token->kind == kind &&
        *(token->next->str) == reserved)
    {
        Token *token_old = token;
        token = token_old->next;
        return token_old;
    }
    return NULL;
}

// 次のトークンが引数のトークンの種類であれば読み進め、そうでなければerror_atを呼び出す
Token *expect_tokenkind(TokenKind kind)
{
    Token *new = consume_tokenkind(kind);
    if (!new)
    {
        char *tokenkindlist[TK_END] = {TokenKindTable};
        error_at(token->str, token->len, "トークンが %s ではありませんでした", tokenkindlist[kind]);
    }
    return new;
}

// 次のトークンが引数のトークンの種類だったら読み勧めてそのTokenを返す
Token *consume_tokenkind(TokenKind kind)
{
    if (token->kind != kind)
        return NULL;
    token_old = token;
    token = token->next;
    return token_old;
}

// 次のトークンが引数の記号だったら読み進めtokenをその他のときはNULLを返す関数
Token *consume(char *op)
{
    if (token->kind != TK_RESERVED ||
        strlen(op) != token->len ||
        memcmp(op, token->str, token->len))
        return NULL;
    token_old = token;
    token = token->next;
    return token_old;
}

// 次のトークンが引数の記号だったら読み進め、そうでなければerror_atを呼び出す
Token *expect(char *op)
{
    Token *result = consume(op);
    if (!result)
        error_at(token->str, token->len, "トークンが %.*s でありませんでした", token->len, op);
    return result;
}

// 一つ前のトークンを取得する
Token *get_old_token()
{
    if (token_old->next != token)
        unreachable();
    return token_old;
}

// 次のトークンが整数だった場合読み進め、それ以外だったらエラーを返す関数
long expect_number()
{
    if (token->kind != TK_NUM)
        error_at(token->str, token->len, "トークンが整数でありませんでした");
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
Token *tokenizer(char *input)
{
    pr_debug("start tokenizer...");
    Token head;
    head.next = NULL;
    Token *cur = &head;
#ifdef PREPROCESS
    nest_list = vector_new();
#endif
    while (*input)
    {
#ifdef PREPROCESS
        // 改行 isspaceでは'\n'も処理されてしまうのでそれより前に置く
        if (*input == '\n')
        {
            cur = new_token(TK_LINEBREAK, cur, input++);
            cur->len = 1;
            continue;
        }

        size_t space_counter = 0;
        while (*input == ' ')
        {
            space_counter++;
            input++;
        }
        if (space_counter)
        {
            cur = new_token(TK_IGNORABLE, cur, input - space_counter);
            cur->len = space_counter;
            continue;
        }
#else
        if (isspace(*input))
        {
            input++;
            continue;
        }
#endif

        // コメントを読み飛ばす
        if (!strncmp(input, "//", 2))
        {
            input++;
#ifdef PREPROCESS
            size_t i = 1;
            while (*++input != '\n')
                i++;
            cur = new_token(TK_IGNORABLE, cur, input - i);
#else
            while (*++input != '\n')
                ;
#endif
            continue;
        }
        if (!strncmp(input, "/*", 2))
        {
            char *end = strstr(input + 2, "*/");
            if (!end)
                error_at(input, 1, "コメントが閉じられていません");
#ifdef PREPROCESS
            cur = new_token(TK_IGNORABLE, cur, input);
            char *pointer = input;
            while (pointer <= end)
            {
                if (*++pointer == '\n')
                {
                    cur->len = pointer - input;
                    cur = new_token(TK_LINEBREAK, cur, pointer);
                    cur->len = 1;
                    cur = new_token(TK_IGNORABLE, cur, pointer);
                    input = pointer;
                }
            }
            cur->len = end + 2 - input;
#endif
            input = end + 2;
            continue;
        }

#ifdef PREPROCESS
        if (*input == '#')
        {
            cur = new_token(TK_DIRECTIVE, cur, input);
            size_t counter = 1;
            while (is_alnum(*++input))
                counter++;
            cur->len = counter;
            // Conditional_Inclusion (#if #endif 等)を高速化するためにまとめる
            if ((counter == 3 && !strncmp(cur->str, "#if", 3)) ||
                (counter == 6 && !strncmp(cur->str, "#ifdef", 6)) ||
                (counter == 7 && !strncmp(cur->str, "#ifndef", 7)))
            {
                Vector *new = vector_new();
                vector_push(Conditional_Inclusion_List, new);
                vector_push(nest_list, new);
                vector_push(vector_peek(nest_list), cur);
                continue;
            }
            if ((counter == 5 && (!strncmp(cur->str, "#else", 5) || !strncmp(cur->str, "#elif", 5))) ||
                (counter == 8 && !strncmp(cur->str, "#elifdef", 8)) ||
                (counter == 9 && !strncmp(cur->str, "#elifndef", 9)))
            {
                vector_push(vector_peek(nest_list), cur);
                continue;
            }
            if (counter == 6 && !strncmp(cur->str, "#endif", 6))
            {
                vector_push(vector_pop(nest_list), cur);
                continue;
            }
            continue;
        }
#endif

        if (strchr("+-*/()=!<>;{},&[].\\", *input))
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
#ifdef PREPROCESS
            int i = 1;
            while (isdigit(*++input))
                i++;
            cur = new_token(TK_IDENT, cur, input - i);
            cur->len = i;
#else
            cur = new_token(TK_NUM, cur, input);
            cur->val = strtol(input, &input, 10);
            pr_debug2("find NUM token: %ld", cur->val);
#endif
            continue;
        }

        // 文字列の検知
        if (*input == '"')
        {
            int i = 0; // 文字列のサイズ("は除く)
            while (*(++input) != '"')
                i++;
            input++; // 終了まで送る
            cur = new_token(TK_STRING, cur, input - i - 2);
            cur->len = i + 2;
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

                if (!strncmp(input - i, "int", i))
                {
                    cur = new_token(TK_INT, cur, input - i);
                    cur->len = i;
                    continue;
                }
            }

            if (i == 4)
            {
                if (!strncmp(input - i, "char", i))
                {
                    cur = new_token(TK_CHAR, cur, input - i);
                    cur->len = i;
                    continue;
                }
                if (!strncmp(input - i, "long", i))
                {
                    cur = new_token(TK_LONG, cur, input - i);
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

                if (!strncmp(input - i, "sizeof", i))
                {
                    cur = new_token(TK_SIZEOF, cur, input - i);
                    cur->val = i;
                    continue;
                }
            }

            cur = new_token(TK_IDENT, cur, input - i);
            cur->len = i;
            continue;
        }

        error_at(input, 1, "トークナイズに失敗しました");
    }

    new_token(TK_EOF, cur, input);

    pr_debug("complite tokenize");
#ifdef PREPROCESS
    vector_free(nest_list);
#endif
#ifdef DEBUG
    print_tokenize_result(head.next);
#endif
    token = head.next;
    return token;
}
