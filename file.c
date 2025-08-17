// ------------------------------------------------------------------------------------
// include file
// ------------------------------------------------------------------------------------

#include "include/file.h"

#ifdef SELF_HOST
#include "test/compiler_header.h"
#else
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

#include "include/error.h"

char *file_read(FILE *fin)
{
  // verify file size
  if (fseek(fin, 0, SEEK_END) == -1)
    error_exit("failed to seek file pointer to end-of-file: %s",
               strerror(errno));
  long filesize = ftell(fin);
  if (filesize == -1)
    error_exit("failed to read file pointer location: %s", strerror(errno));

  if (fseek(fin, 0, SEEK_SET) == -1)
    error_exit("failed to seek file pointer to start-of-file: %s",
               strerror(errno));
  pr_debug("file size: %ld", filesize);

  char *buf = calloc(1, filesize + 2);
  fread(buf, filesize, 1, fin);
  if (filesize == 0 ||
      buf[filesize - 1] != '\n')  // make sure the file ends with "\n\0"
    buf[filesize++] = '\n';       // rewrite if the last is EOF
  buf[filesize] = '\0';
  pr_debug2("file content:\n%s", buf);
  return buf;
}

// Read the file with the argument file name and return it as a char array.
// Internally uses calloc
char *openfile(char *filename)
{
  FILE *fin = fopen(filename, "r");
  if (!fin)
  {
    error_exit("Invalid filename: %s", filename);
  }
  pr_debug("File opened.");
  return file_read(fin);
}