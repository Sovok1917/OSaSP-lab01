// dirwalk.c
#define _XOPEN_SOURCE 500       // Required for nftw()
#define _POSIX_C_SOURCE 200809L // For POSIX compliance

#include <ftw.h>       // For nftw()
#include <stdio.h>     // For standard I/O functions
#include <stdlib.h>    // For memory allocation functions
#include <string.h>    // For string manipulation functions
#include <locale.h>    // For locale settings
#include <errno.h>     // For error handling
#include <unistd.h>    // For readlink, lstat, stat
#include <limits.h>    // For PATH_MAX
#include "dirwalk.h"   // For flag definitions and function prototypes

// Global variables
static int flags;  // Stores the option flags
static int (*output_function)(const char *fpath);  // Function pointer for output (either puts or add_to_list)
static char *original_start_dir;  // The original starting directory path provided by the user
static char *traverse_dir;        // The actual directory to traverse (target if symlink)

// Structure to store file paths when sorting is required
typedef struct {
    char **items;     // Array of strings (file paths)
    size_t capacity;  // Current allocated capacity
    size_t count;     // Number of items stored
} FileList;

static FileList file_list = {NULL, 0, 0};  // Initialize file list

// Function to remove trailing slashes from a path
static void remove_trailing_slash(char *path) {
    size_t len = strlen(path);
    while (len > 0 && path[len - 1] == '/') {
        path[len - 1] = '\0';
        len--;
    }
}

// Function to initialize the file list
static void init_file_list() {
    file_list.capacity = 100;  // Starting capacity
    file_list.count = 0;
    file_list.items = malloc(file_list.capacity * sizeof(char *));
    if (!file_list.items) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
}

// Function to add a file path to the list
static int add_to_list(const char *fpath) {
    if (file_list.count >= file_list.capacity) {
        file_list.capacity *= 2;
        char **temp = realloc(file_list.items, file_list.capacity * sizeof(char *));
        if (!temp) {
            perror("realloc");
            return 1; // Signal to stop traversal due to memory allocation failure
        }
        file_list.items = temp;
    }
    file_list.items[file_list.count++] = strdup(fpath);
    return 0;
}

// Comparison function for qsort()
static int compare_func(const void *a, const void *b) {
    const char *pa = *(const char **)a;
    const char *pb = *(const char **)b;
    return strcoll(pa, pb);
}

// Function to sort and print the file list
static void sort_and_print() {
    setlocale(LC_COLLATE, "");
    qsort(file_list.items, file_list.count, sizeof(char *), compare_func);
    for (size_t i = 0; i < file_list.count; i++) {
        puts(file_list.items[i]);
        free(file_list.items[i]);
    }
}

// Function to free the file list memory
static void free_file_list() {
    free(file_list.items);
}

// Callback function for nftw()
static int process_entry(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
    (void) sb;     // Mark sb as unused
    (void) ftwbuf; // Mark ftwbuf as unused to suppress warning
    int print = 0;
    char *adjusted_path;

    // Determine if this entry should be printed based on flags
    switch (typeflag) {
        case FTW_SL:
        case FTW_SLN:
            if (flags & FLAG_LINKS) print = 1;
            break;
        case FTW_D:
            if (flags & FLAG_DIRS) print = 1;
            break;
        case FTW_F:
            if (flags & FLAG_FILES) print = 1;
            break;
        default:
            break;
    }

    if (print) {
        // Adjust the path to match find's output
        if (strcmp(traverse_dir, original_start_dir) != 0) {
            size_t len = strlen(traverse_dir);
            if (strncmp(fpath, traverse_dir, len) == 0) {
                if (fpath[len] == '\0') {
                    // This is the starting directory itself
                    adjusted_path = strdup(original_start_dir);
                } else if (fpath[len] == '/') {
                    // This is a subentry, adjust the path
                    char *relative_path = (char *)fpath + len + 1;
                    adjusted_path = malloc(strlen(original_start_dir) + 1 + strlen(relative_path) + 1);
                    if (adjusted_path) {
                        sprintf(adjusted_path, "%s/%s", original_start_dir, relative_path);
                    }
                } else {
                    adjusted_path = strdup(fpath); // Fallback
                }
            } else {
                adjusted_path = strdup(fpath); // Fallback
            }
        } else {
            adjusted_path = strdup(fpath); // No adjustment needed
        }

        if (!adjusted_path) {
            perror("malloc");
            return 1; // Signal to stop traversal
        }

        // Output the adjusted path
        int result = output_function(adjusted_path);
        free(adjusted_path);
        if (result == EOF || result == 1) { // EOF from puts or 1 from add_to_list on error
            fprintf(stderr, "Error processing '%s'\n", fpath);
            return 1; // Signal to stop traversal
        }
    }

    return 0; // Continue traversal
}

// Function to perform directory traversal
void dirwalk(const char *path, int dirwalk_flags) {
    flags = dirwalk_flags;

    // Store the original starting directory
    original_start_dir = strdup(path);
    if (!original_start_dir) {
        perror("strdup");
        exit(EXIT_FAILURE);
    }
    remove_trailing_slash(original_start_dir);

    // Check if the starting directory is a symbolic link
    struct stat lstat_buf;
    if (lstat(original_start_dir, &lstat_buf) == -1) {
        perror("lstat");
        free(original_start_dir);
        exit(EXIT_FAILURE);
    }

    if (S_ISLNK(lstat_buf.st_mode)) {
        // Read the target of the symbolic link
        char target[PATH_MAX];
        ssize_t len = readlink(original_start_dir, target, sizeof(target) - 1);
        if (len == -1) {
            perror("readlink");
            free(original_start_dir);
            exit(EXIT_FAILURE);
        }
        target[len] = '\0'; // Null-terminate

        // Check if the target is a directory
        struct stat stat_buf;
        if (stat(target, &stat_buf) == -1) {
            perror("stat");
            free(original_start_dir);
            exit(EXIT_FAILURE);
        }
        if (S_ISDIR(stat_buf.st_mode)) {
            // Target is a directory, traverse it
            traverse_dir = strdup(target);
            if (!traverse_dir) {
                perror("strdup");
                free(original_start_dir);
                exit(EXIT_FAILURE);
            }
            remove_trailing_slash(traverse_dir);
        } else {
            // Target is not a directory, traverse the symlink itself
            traverse_dir = strdup(original_start_dir);
            if (!traverse_dir) {
                perror("strdup");
                free(original_start_dir);
                exit(EXIT_FAILURE);
            }
        }
    } else {
        // Not a symbolic link, traverse the original path
        traverse_dir = strdup(original_start_dir);
        if (!traverse_dir) {
            perror("strdup");
            free(original_start_dir);
            exit(EXIT_FAILURE);
        }
    }

    // Set up output function based on sorting flag
    if (flags & FLAG_SORT) {
        init_file_list();
        output_function = add_to_list;
    } else {
        output_function = puts;
    }

    // Perform the traversal
    if (nftw(traverse_dir, process_entry, 20, FTW_PHYS) == -1) {
        fprintf(stderr, "nftw error on '%s': %s\n", traverse_dir, strerror(errno));
        free(original_start_dir);
        free(traverse_dir);
        exit(EXIT_FAILURE);
    }

    // If sorting, print the sorted list
    if (flags & FLAG_SORT) {
        sort_and_print();
        free_file_list();
    }

    // Clean up
    free(original_start_dir);
    free(traverse_dir);
}
