#!/bin/sh

gcc -E -xc - -v < /dev/null 2>&1 | sed -n '/#include <...> search starts here:/,/End of search list./p' | grep '^ /' | sed 's/^ *//' | awk '
BEGIN {
    print "#ifndef INCLUDE_PATHS_H"
    print "#define INCLUDE_PATHS_H"
    print ""
    print "#include <stddef.h>"
    print ""
    i = 1
    max_len = 0
    longest_path_var = ""
}
{
    path = $0 "/"
    paths[i] = path
    print "static char include_path_" i "[] = \"" path "\";"
    if (length(path) > max_len) {
        max_len = length(path)
        longest_path_var = "include_path_" i
    }
    i++
}
END {
    print ""
    print "static char *lib_path[] = {"
    for (j = 1; j < i; j++) {
        print "    include_path_" j ","
    }
    print "};"
    print ""
    print "static size_t lib_path_size[] = {"
    for (j = 1; j < i; j++) {
        print "    sizeof(include_path_" j "),"
    }
    print "};"
    print ""
    print "#define MAX_LIB_PATH_SIZE sizeof(" longest_path_var ")"
    print ""
    print "#endif // INCLUDE_PATHS_H"
}
' > include/include_paths.h

gcc -dM -E - < /dev/null > include/gcc_predef.h