#include <stdio.h>
#include <stdlib.h>

#define max_size 8

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
        if (i == max_size) {
            fprintf(stderr, "引数が多すぎます!!!\n");
            break;
        }
    }

    fprintf(fout, ".intel_syntax noprefix\n");
    fprintf(fout, ".global main\n");
    fprintf(fout, "main:\n");
    fprintf(fout, "   mov rax, %d\n", atoi(tmp));
    fprintf(fout, "   ret\n");

    fclose(fin);
    fclose(fout);
    return 0;
}