/*
 * dirwalk: Recursively scans a directory and prints file paths based on type filters.
 *
 * Usage: dirwalk [dir] [-l] [-d] [-f] [-s]
 *   dir: Starting directory (default: current directory "./").
 *   -l:  List only symbolic links.
 *   -d:  List only directories.
 *   -f:  List only regular files.
 *   -s:  Sort the output according to LC_COLLATE.
 *
 * If no type options (-l, -d, -f) are given, all entry types (files, directories,
 * links, sockets, fifos, etc.) are listed, similar to find's default behavior.
 * Options can be combined (e.g., -ld) and appear before or after the directory.
 * The output format matches the 'find' utility for the equivalent options.
 */

#define _POSIX_C_SOURCE 200809L // Required for feature test macros like S_ISLNK, strdup

#include <stdio.h>      // printf, fprintf, stderr, perror, snprintf
#include <stdlib.h>     // malloc, realloc, free, exit, qsort, getenv, abort
#include <string.h>     // strcmp, strdup, strlen, strcoll, strerror
#include <dirent.h>     // opendir, readdir, closedir, DIR, struct dirent
#include <sys/stat.h>   // lstat, struct stat, S_ISLNK, S_ISDIR, S_ISREG
#include <unistd.h>     // getopt
#include <errno.h>      // errno
#include <locale.h>     // setlocale, LC_COLLATE

// Initial capacity for the results array when sorting
#define INITIAL_RESULTS_CAPACITY 64

// Structure to hold results for sorting
typedef struct results_s {
    char **paths;    // Dynamically allocated array of path strings
    size_t count;    // Number of paths currently stored
    size_t capacity; // Allocated capacity of the paths array
} results_t;

// Function Prototypes
static void print_usage(const char *prog_name);
static int compare_strings(const void *a, const void *b);
static void add_result(results_t *results, const char *path);
static void free_results(results_t *results);
static int process_entry(const char *path, int show_l, int show_d, int show_f,
                         int explicit_type_filter, int sort_output, results_t *results);
static void walk_directory_contents(const char *dir_path, int show_l, int show_d, int show_f,
                                    int explicit_type_filter, int sort_output, results_t *results);

/*
 * main: Entry point of the program. Parses command-line arguments,
 *       initializes structures, processes the starting path, calls the
 *       directory walking function if applicable, handles sorting and printing
 *       of results, and cleans up resources.
 *
 * Parameters:
 *   argc - The number of command-line arguments.
 *   argv - An array of command-line argument strings.
 *
 * Returns:
 *   EXIT_SUCCESS (0) on successful completion.
 *   EXIT_FAILURE (1) if an error occurs.
 */
int main(int argc, char *argv[]) {
    int opt;
    int show_l = 0;
    int show_d = 0;
    int show_f = 0;
    int sort_output = 0;
    int explicit_type_filter = 0; // Flag: Was -l, -d, or -f explicitly given?
    const char *start_dir = "."; // Default starting directory
    results_t results = {NULL, 0, 0}; // Initialize results structure for sorting
    struct stat start_stat; // To check the type of the starting path

    // Set locale for strcoll sorting and potentially multibyte characters
    if (setlocale(LC_COLLATE, "") == NULL) {
        fprintf(stderr, "Warning: Failed to set locale, sorting might be incorrect.\n");
    }

    // --- Argument Parsing (Handling options before/after directory) ---
    optind = 1; // Ensure getopt starts from the beginning

    // Pass 1: Parse options that appear *before* the directory argument
    while ((opt = getopt(argc, argv, "ldfs")) != -1) {
        switch (opt) {
            case 'l': show_l = 1; explicit_type_filter = 1; break;
            case 'd': show_d = 1; explicit_type_filter = 1; break;
            case 'f': show_f = 1; explicit_type_filter = 1; break;
            case 's': sort_output = 1; break;
            case '?': // Invalid option
            default:
                print_usage(argv[0]);
                return EXIT_FAILURE;
        }
    }

    // Check if a non-option argument (the directory) exists
    if (optind < argc) {
        // Assume the first non-option is the directory
        start_dir = argv[optind];
        optind++; // Consume the directory argument index

        // Pass 2: Continue parsing options that appear *after* the directory argument
        // getopt will naturally continue from the current optind
        while ((opt = getopt(argc, argv, "ldfs")) != -1) {
            switch (opt) {
                case 'l': show_l = 1; explicit_type_filter = 1; break;
                case 'd': show_d = 1; explicit_type_filter = 1; break;
                case 'f': show_f = 1; explicit_type_filter = 1; break;
                case 's': sort_output = 1; break;
                case '?': // Invalid option
                default:
                    fprintf(stderr, "Error: Invalid option '%c' after directory argument.\n", optopt);
                    print_usage(argv[0]);
                    return EXIT_FAILURE;
            }
        }
    }
    // Else: No directory argument found after initial options, use default "."

    // Check for any remaining non-option arguments (errors)
    if (optind < argc) {
        fprintf(stderr, "Error: Unexpected argument '%s'\n", argv[optind]);
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    // --- End Argument Parsing ---


    // Initialize results array if sorting is enabled
    if (sort_output) {
        results.paths = (char **)malloc(INITIAL_RESULTS_CAPACITY * sizeof(char *));
        if (results.paths == NULL) {
            perror("Error allocating initial results array");
            abort();
        }
        results.capacity = INITIAL_RESULTS_CAPACITY;
        results.count = 0;
    }

    // --- Core Logic ---

    // 1. Process the starting path itself first.
    process_entry(start_dir, show_l, show_d, show_f, explicit_type_filter, sort_output, sort_output ? &results : NULL);

    // 2. Check the type of the starting path to see if we should descend into it.
    if (lstat(start_dir, &start_stat) == 0) {
        // If the starting path is a directory (and not a symlink to one),
        // proceed to walk its contents recursively.
        if (S_ISDIR(start_stat.st_mode)) {
            walk_directory_contents(start_dir, show_l, show_d, show_f, explicit_type_filter, sort_output, sort_output ? &results : NULL);
        }
        // If it's not a directory (file, link, socket, etc.), we've already processed it
        // with process_entry above, so we do nothing more.
    } else {
        // lstat failed on the starting path. process_entry already printed an error.
        // We cannot walk its contents. Exit with failure status.
        if (sort_output) {
            // Sort and print whatever might have been added before the error
            qsort(results.paths, results.count, sizeof(char *), compare_strings);
            for (size_t i = 0; i < results.count; ++i) {
                printf("%s\n", results.paths[i]);
            }
            free_results(&results);
        }
        return EXIT_FAILURE; // Indicate an error occurred
    }
    // --- End Core Logic ---

    // If sorting, sort and print the collected results
    if (sort_output) {
        qsort(results.paths, results.count, sizeof(char *), compare_strings);
        for (size_t i = 0; i < results.count; ++i) {
            printf("%s\n", results.paths[i]);
        }
        free_results(&results); // Free memory allocated for results
    }

    return EXIT_SUCCESS;
}

/*
 * print_usage: Prints help message to stderr.
 *
 * Parameters:
 *   prog_name - The name of the program (argv[0]).
 *
 * Returns:
 *   Nothing.
 */
static void print_usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s [dir] [-l] [-d] [-f] [-s]\n", prog_name);
    fprintf(stderr, "  dir: Starting directory (default: .)\n");
    fprintf(stderr, "  -l:  List only symbolic links.\n");
    fprintf(stderr, "  -d:  List only directories.\n");
    fprintf(stderr, "  -f:  List only regular files.\n");
    fprintf(stderr, "  -s:  Sort output by name (LC_COLLATE).\n");
    fprintf(stderr, "If no type options (-l, -d, -f) are given, all types are listed.\n");
}

/*
 * compare_strings: Comparison function for qsort, using locale-aware comparison.
 *
 * Parameters:
 *   a - Pointer to the first string (char **).
 *   b - Pointer to the second string (char **).
 *
 * Returns:
 *   An integer based on strcoll comparison.
 */
static int compare_strings(const void *a, const void *b) {
    return strcoll(*(const char **)a, *(const char **)b);
}

/*
 * add_result: Adds a path string to the dynamic results array, resizing if necessary.
 *
 * Parameters:
 *   results - Pointer to the results_t structure.
 *   path    - The path string to add (will be duplicated).
 *
 * Returns:
 *   Nothing. Aborts on memory allocation failure.
 */
static void add_result(results_t *results, const char *path) {
    if (results == NULL) return;

    if (results->count >= results->capacity) {
        size_t new_capacity = results->capacity * 2;
        if (new_capacity <= results->capacity) {
            fprintf(stderr, "Error: Result capacity overflow for results array\n");
            abort();
        }
        char **new_paths = (char **)realloc(results->paths, new_capacity * sizeof(char *));
        if (new_paths == NULL) {
            perror("Error reallocating results array");
            free_results(results);
            abort();
        }
        results->paths = new_paths;
        results->capacity = new_capacity;
    }

    results->paths[results->count] = strdup(path);
    if (results->paths[results->count] == NULL) {
        perror("Error duplicating path string");
        free_results(results);
        abort();
    }
    results->count++;
}

/*
 * free_results: Frees the memory allocated for the results array and its contents.
 *
 * Parameters:
 *   results - Pointer to the results_t structure.
 *
 * Returns:
 *   Nothing.
 */
static void free_results(results_t *results) {
    if (results && results->paths) {
        for (size_t i = 0; i < results->count; ++i) {
            free(results->paths[i]);
        }
        free(results->paths);
        results->paths = NULL;
        results->count = 0;
        results->capacity = 0;
    }
}

/*
 * process_entry: Checks a file system entry against the filters and either prints
 *                its path or adds it to the results list. Uses lstat.
 *
 * Parameters:
 *   path                 - The full path to the file system entry.
 *   show_l               - Flag indicating whether to list symbolic links.
 *   show_d               - Flag indicating whether to list directories.
 *   show_f               - Flag indicating whether to list regular files.
 *   explicit_type_filter - Flag indicating if -l, -d, or -f was specified.
 *   sort_output          - Flag indicating whether output should be sorted.
 *   results              - Pointer to the results_t structure (if sorting).
 *
 * Returns:
 *    1 if the entry was processed (printed or added).
 *    0 otherwise (including lstat errors or filter mismatch).
 *   Prints error messages to stderr if lstat fails.
 */
static int process_entry(const char *path, int show_l, int show_d, int show_f,
                         int explicit_type_filter, int sort_output, results_t *results) {
    struct stat stat_buf;
    int processed = 0;

    if (lstat(path, &stat_buf) == -1) {
        fprintf(stderr, "Error getting status for '%s': %s\n", path, strerror(errno));
        return 0;
    }

    int should_output = 0;

    // --- Logic Change: Handle default (all types) vs explicit filters ---
    if (!explicit_type_filter) {
        // Default behavior: No specific type flags given (-l, -d, -f). Output everything.
        should_output = 1;
    } else {
        // Explicit filters (-l, -d, -f) were given. Check only those types.
        if (S_ISLNK(stat_buf.st_mode) && show_l) {
            should_output = 1;
        } else if (S_ISDIR(stat_buf.st_mode) && show_d) {
            should_output = 1;
        } else if (S_ISREG(stat_buf.st_mode) && show_f) {
            should_output = 1;
        }
        // Other types (sockets, fifos, etc.) are implicitly ignored when explicit filters are used.
    }
    // --- End Logic Change ---

    if (should_output) {
        if (sort_output) {
            add_result(results, path);
        } else {
            if (printf("%s\n", path) < 0) {
                perror("Error writing to stdout");
                if (sort_output) free_results(results);
                exit(EXIT_FAILURE);
            }
        }
        processed = 1;
    }
    return processed;
                         }


                         /*
                          * walk_directory_contents: Recursively traverses the *contents* of a directory.
                          *
                          * Parameters:
                          *   dir_path             - Path to the directory whose contents are traversed.
                          *   show_l               - Flag: list symbolic links.
                          *   show_d               - Flag: list directories.
                          *   show_f               - Flag: list regular files.
                          *   explicit_type_filter - Flag: were -l, -d, or -f specified?
                          *   sort_output          - Flag: sort output?
                          *   results              - Pointer to results structure (if sorting).
                          *
                          * Returns:
                          *   Nothing. Prints error messages to stderr for directory access issues.
                          */
                         static void walk_directory_contents(const char *dir_path, int show_l, int show_d, int show_f,
                                                             int explicit_type_filter, int sort_output, results_t *results) {
                             DIR *dir_stream = NULL;
                             struct dirent *entry = NULL;

                             dir_stream = opendir(dir_path);
                             if (dir_stream == NULL) {
                                 fprintf(stderr, "Error opening directory '%s': %s\n", dir_path, strerror(errno));
                                 return;
                             }

                             errno = 0;
                             while ((entry = readdir(dir_stream)) != NULL) {
                                 if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                                     continue;
                                 }

                                 size_t dir_len = strlen(dir_path);
                                 int needs_slash = (dir_len > 0 && dir_path[dir_len - 1] != '/');
                                 size_t name_len = strlen(entry->d_name);
                                 size_t full_path_len = dir_len + (needs_slash ? 1 : 0) + name_len + 1;

                                 char *full_path = (char *)malloc(full_path_len);
                                 if (full_path == NULL) {
                                     perror("Error allocating memory for path string");
                                     if (sort_output) free_results(results);
                                     closedir(dir_stream);
                                     abort();
                                 }

                                 int written = snprintf(full_path, full_path_len, "%s%s%s",
                                                        dir_path, (needs_slash ? "/" : ""), entry->d_name);

                                 if (written < 0 || (size_t)written >= full_path_len) {
                                     fprintf(stderr, "Error constructing path for '%s' in '%s'\n", entry->d_name, dir_path);
                                     free(full_path);
                                     errno = 0;
                                     continue;
                                 }

                                 // 1. Process this entry (file, link, dir, socket, etc.)
                                 process_entry(full_path, show_l, show_d, show_f, explicit_type_filter, sort_output, results);

                                 // 2. If the entry is a directory (use lstat to check without following links), recurse.
                                 struct stat entry_stat;
                                 if (lstat(full_path, &entry_stat) == 0) {
                                     if (S_ISDIR(entry_stat.st_mode)) {
                                         walk_directory_contents(full_path, show_l, show_d, show_f, explicit_type_filter, sort_output, results);
                                     }
                                 } else {
                                     // lstat failed on this entry. process_entry already printed an error.
                                     // Cannot determine if it's a directory, so cannot recurse.
                                     ; // Error handled in process_entry
                                 }

                                 free(full_path);
                                 full_path = NULL;
                                 errno = 0; // Reset errno before next readdir
                             }

                             if (errno != 0) {
                                 fprintf(stderr, "Error reading directory '%s': %s\n", dir_path, strerror(errno));
                             }

                             if (closedir(dir_stream) == -1) {
                                 fprintf(stderr, "Error closing directory '%s': %s\n", dir_path, strerror(errno));
                             }
                                                             }
