// ------------------------------------------------------------------------------------
// include file
// ------------------------------------------------------------------------------------

#include "include/file.h"
#include "include/error.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

// 引数のファイル名のファイルを読み取り、char配列として返す 内部でcallocを利用している
char *openfile(char *filename)
{
    FILE *fin = fopen(filename, "r");
    if (!fin)
    {
        error_exit("ファイル名が正しくありません");
    }
    pr_debug("file open");
    // ファイルサイズ検証
    if (fseek(fin, 0, SEEK_END) == -1)
        error_exit("ファイルポインタをファイル末尾に移動するのに失敗しました: %s", strerror(errno));
    long filesize = ftell(fin);
    if (filesize == -1)
        error_exit("ファイルポインタの位置を読み取るのに失敗しました: %s", strerror(errno));

    if (fseek(fin, 0, SEEK_SET) == -1)
        error_exit("ファイルポインタをファイル先頭に移動するのに失敗しました: %s", strerror(errno));
    pr_debug("file size: %ld", filesize);

    char *buf = calloc(1, filesize + 2);
    fread(buf, filesize, 1, fin);
    if (filesize == 0 || buf[filesize - 1] != '\n') // ファイルの最後が"\n\0"になるように
        buf[filesize++] = '\n';                     // 最後がEOFなら書き換える
    buf[filesize] = '\0';
    pr_debug2("file content:\n%s", buf);
    return buf;
}