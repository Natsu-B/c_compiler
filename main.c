#include <stdio.h>
#include <stdlib.h>

#define max_size 100

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "引数が正しくありません\n");
        return 1;
    }
    FILE *fin = fopen(argv[1], "r");
    FILE *fout = fopen("out.s", "w");
    if (fin == NULL)
    {
        fprintf(stderr, "ファイル名が正しくありません\n");
        return 1;
    }
    if (fout == NULL)
    {
        fprintf(stderr, "ファイルに書き込めませんでした\n");
        return 1;
    }
    char tmp[max_size + 1] = {};
    int i = 0;
    while (!feof(fin))
    {
        tmp[i] = fgetc(fin);
        i++;
        if (i == max_size)
        {
            fprintf(stderr, "引数が多すぎます!!!\n");
            break;
        }
    }
    char *p = tmp;

    fprintf(fout, ".intel_syntax noprefix\n");
    fprintf(fout, ".global main\n");
    fprintf(fout, "main:\n");
    fprintf(fout, "   mov rax, %ld\n", strtol(p, &p, 10));

    while (*p)
    {
        if (*p == '+')
        {
            p++;
            fprintf(fout, "    add rax, %ld\n", strtol(p, &p, 10));
            continue;
        }
        if (*p == '-')
        {
            p++;
            fprintf(fout, "    sub rax, %ld\n", strtol(p, &p, 10));
        }
        if (*p == '\n') // '\n'
            break;
        fprintf(stderr, "予期せぬ文字です: %c\n", *p);
        return 1;
    }

    fprintf(fout, "   ret\n");

    fclose(fin);
    fclose(fout);
    return 0;
}