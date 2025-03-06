// main.c
// Description: This program performs a directory traversal, listing files, directories, and symbolic links based on specified options.
//              It supports options to filter the output to include only files, directories, or links, and an option to sort the output.
//              The program uses the dirwalk function from dirwalk.c to perform the traversal and process each entry.

// Required for POSIX compliance
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dirwalk.h"

int main(int argc, char *argv[]) {
    // Initialize all flags by default (links, directories, files, sorting)
    int flags = FLAG_LINKS | FLAG_DIRS | FLAG_FILES;
    char *start_dir = ".";         // Default starting directory
    int dir_specified = 0;         // Indicates if directory argument has been provided
    int type_flags_provided = 0;   // Tracks if any type options (-l, -d, -f) have been specified

    // Loop through all command-line arguments
    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];

        if (arg[0] == '-') {
            // Argument is an option (starts with '-')
            // Process combined options (e.g., -ld)
            for (int j = 1; arg[j] != '\0'; j++) {
                switch (arg[j]) {
                    case 'l':
                    case 'd':
                    case 'f':
                        // If this is the first type option, reset type flags
                        if (!type_flags_provided) {
                            flags &= ~(FLAG_LINKS | FLAG_DIRS | FLAG_FILES);
                            type_flags_provided = 1;
                        }
                        // Set the appropriate flag based on the option
                        if (arg[j] == 'l') flags |= FLAG_LINKS;
                        if (arg[j] == 'd') flags |= FLAG_DIRS;
                        if (arg[j] == 'f') flags |= FLAG_FILES;
                        break;
                    case 's':
                        // Enable sorting flag
                        flags |= FLAG_SORT;
                        break;
                    default:
                        // Unknown option encountered
                        fprintf(stderr, "Unknown option: -%c\n", arg[j]);
                        fprintf(stderr, "Usage: %s [dir] [options]\n", argv[0]);
                        exit(EXIT_FAILURE);
                }
            }
        } else {
            // Argument is not an option
            if (!dir_specified) {
                // First non-option argument is the directory
                start_dir = arg;
                dir_specified = 1;
            } else {
                // If directory already specified, check if the argument starts with '-'
                if (arg[0] == '-') {
                    // Process options after the directory
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
                    // Unexpected argument encountered
                    fprintf(stderr, "Unexpected argument: %s\n", arg);
                    fprintf(stderr, "Usage: %s [dir] [options]\n", argv[0]);
                    exit(EXIT_FAILURE);
                }
            }
        }
    }

    // Call the dirwalk function with the starting directory and flags
    dirwalk(start_dir, flags);

    return 0;
}

