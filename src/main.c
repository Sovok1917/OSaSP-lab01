// main.c
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dirwalk.h"

int main(int argc, char *argv[]) {
    // Initialize all flags by default
    int flags = FLAG_LINKS | FLAG_DIRS | FLAG_FILES | FLAG_SORT;
    char *start_dir = ".";
    int dir_specified = 0;
    int type_flags_provided = 0;

    // Loop through all arguments
    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];

        if (arg[0] == '-') {
            // Process options (can be combined like -ld)
            for (int j = 1; arg[j] != '\0'; j++) {
                switch (arg[j]) {
                    case 'l':
                    case 'd':
                    case 'f':
                        // On the first type option, reset type flags
                        if (!type_flags_provided) {
                            flags &= ~(FLAG_LINKS | FLAG_DIRS | FLAG_FILES);
                            type_flags_provided = 1;
                        }
                        if (arg[j] == 'l') flags |= FLAG_LINKS;
                        if (arg[j] == 'd') flags |= FLAG_DIRS;
                        if (arg[j] == 'f') flags |= FLAG_FILES;
                        break;
                    case 's':
                        flags |= FLAG_SORT;
                        break;
                    default:
                        fprintf(stderr, "Unknown option: -%c\n", arg[j]);
                        fprintf(stderr, "Usage: %s [dir] [options]\n", argv[0]);
                        exit(EXIT_FAILURE);
                }
            }
        } else {
            if (!dir_specified) {
                start_dir = arg;
                dir_specified = 1;
            } else {
                // If directory already specified, treat as options
                if (arg[0] == '-') {
                    // Process options after directory
                    for (int j = 1; arg[j] != '\0'; j++) {
                        switch (arg[j]) {
                            case 'l':
                            case 'd':
                            case 'f':
                                if (!type_flags_provided) {
                                    flags &= ~(FLAG_LINKS | FLAG_DIRS | FLAG_FILES);
                                    type_flags_provided = 1;
                                }
                                if (arg[j] == 'l') flags |= FLAG_LINKS;
                                if (arg[j] == 'd') flags |= FLAG_DIRS;
                                if (arg[j] == 'f') flags |= FLAG_FILES;
                                break;
                            case 's':
                                flags |= FLAG_SORT;
                                break;
                            default:
                                fprintf(stderr, "Unknown option: -%c\n", arg[j]);
                                fprintf(stderr, "Usage: %s [dir] [options]\n", argv[0]);
                                exit(EXIT_FAILURE);
                        }
                    }
                } else {
                    fprintf(stderr, "Unexpected argument: %s\n", arg);
                    fprintf(stderr, "Usage: %s [dir] [options]\n", argv[0]);
                    exit(EXIT_FAILURE);
                }
            }
        }
    }

    // Call the dirwalk function
    dirwalk(start_dir, flags);

    return 0;
}

