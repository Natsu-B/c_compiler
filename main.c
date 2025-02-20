#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>

#define max_size 100

#define error(fmt, ...) \
    _error(__FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

typedef enum
{
    TK_RESERVED, // 記号
    TK_NUM,      // 整数
    TK_EOF,      // 入力終了
} TokenKind;     // トークンの種類

typedef struct Token Token;

struct Token
{
    TokenKind kind; // トークンの種類
    Token *next;    // 次のトークン
    char *str;      // トークン文字列
    union           // トークンの種類に応じたデータを保存
    {
        long val; // 整数の場合の値
    };
};

// エラー時にログを出力し、終了する関数 errorから呼び出される
void _error(char *file, int line, const char *func, char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[ERROR] %s:%d:%s(): ", file, line, func);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    exit(1);
}

char *user_input;

void error_init(char *input)
{
    user_input = input;
}

// 入力プログラムがおかしいとき、エラー箇所を可視化するプログラム
void error_at(char *error_location, char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    // エラー位置特定
    int error_position = error_location - user_input;
    fprintf(stderr, "%s", user_input);
    fprintf(stderr, "%*s", error_position, " ");
    fprintf(stderr, "^ ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    exit(1);
}

// 引数のファイル名のファイルを読み取り、char配列として返す 内部でcallocを利用している
char *openfile(char *filename)
{
    FILE *fin = fopen(filename, "r");
    if (!fin)
    {
        error("ファイル名が正しくありません");
    }
    // ファイルサイズ検証
    if (fseek(fin, 0, SEEK_END) == -1)
        error("ファイルポインタをファイル末尾に移動するのに失敗しました: %s", strerror(errno));
    long filesize = ftell(fin);
    if (filesize == -1)
        error("ファイルポインタの位置を読み取るのに失敗しました: %s", strerror(errno));

    if (fseek(fin, 0, SEEK_SET) == -1)
        error("ファイルポインタをファイル先頭に移動するのに失敗しました: %s", strerror(errno));

    char *buf = calloc(1, filesize + 2);
    fread(buf, filesize, 1, fin);
    if (filesize == 0 || buf[filesize - 1] != '\n') // ファイルの最後が"\n\0"になるように
        buf[filesize++] = '\n';                     // 最後がEOFなら書き換える
    buf[filesize] = '\0';
    return buf;
}

// ------------------------------------------------------------------------------------
// ここからトークナイズ関連の処理
// ------------------------------------------------------------------------------------

Token *token; // トークンの実体 利用する関数は制限する

// 次のトークンが引数の記号だったら読み進めtrueをその他のときはfalseを返す関数
bool consume(char op)
{
    if (token->kind != TK_RESERVED || token->str[0] != op)
        return false;

    token = token->next;
    return true;
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
Token *tokenize(char *input)
{
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

        if (*input == '+' || *input == '-')
        {
            cur = new_token(TK_RESERVED, cur, input);
            input++;
            continue;
        }

        if (isdigit(*input))
        {
            cur = new_token(TK_NUM, cur, input);
            cur->val = strtol(input, &input, 10);
            continue;
        }

        if (*input == '\n')
        {
            input++;
            continue;
        }
        error_at(input, "トークナイズに失敗しました");
    }

    new_token(TK_EOF, cur, input);
    return head.next;
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        error("引数が正しくありません");
    }
    char *input = openfile(argv[1]);
    error_init(input);

    token = tokenize(input);
    FILE *fout = fopen("out.s", "w");
    if (fout == NULL)
    {
        error("ファイルに書き込めませんでした");
    }

    fprintf(fout, ".intel_syntax noprefix\n");
    fprintf(fout, ".global main\n");
    fprintf(fout, "main:\n");
    fprintf(fout, "    mov rax, %ld\n", expect_number());

    while (!at_eof())
    {
        if (consume('+'))
        {
            fprintf(fout, "    add rax, %ld\n", expect_number());
            continue;
        }
        if (consume('-'))
        {
            fprintf(fout, "    sub rax, %ld\n", expect_number());
            continue;
        }
        error_at(token->str, "不正なトークン");
    }
    fprintf(fout, "   ret\n");

    fclose(fout);
    return 0;
}