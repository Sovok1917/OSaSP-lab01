// dirwalk.c
#define _XOPEN_SOURCE 500       // Required for nftw()
#define _POSIX_C_SOURCE 200809L // For POSIX compliance

#include <ftw.h>       // For nftw()
#include <stdio.h>     // For standard I/O functions
#include <stdlib.h>    // For memory allocation functions
#include <string.h>    // For string manipulation functions
#include <locale.h>    // For locale settings
#include <errno.h>     // For error handling
#include "dirwalk.h"   // For flag definitions and function prototypes

// Global variables
static int flags;  // Stores the option flags
static int add_to_list(const char *fpath);
static int (*output_function)(const char *fpath);  // Function pointer for output (either puts or add_to_list)

// Structure to store file paths when sorting is required
typedef struct {
    char **items;     // Array of strings (file paths)
    size_t capacity;  // Current allocated capacity
    size_t count;     // Number of items stored
} FileList;

static FileList file_list = {NULL, 0, 0};  // Initialize file list

// Function to initialize the file list
// Initializes the file list with a starting capacity of 100. Allocates memory for the array of file paths.
// Parameters: None
// Returns: None
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
// Takes a file path and adds it to the file list. Resizes the list if necessary.
// Parameters: const char *fpath - the file path to add
// Returns: int - 0 on success, 1 on memory allocation failure
static int add_to_list(const char *fpath) {
    // Check if the list needs to be resized
    if (file_list.count >= file_list.capacity) {
        file_list.capacity *= 2;  // Double the capacity
        char **temp = realloc(file_list.items, file_list.capacity * sizeof(char *));
        if (!temp) {
            perror("realloc");
            return 1; // Signal to stop traversal due to memory allocation failure
        }
        file_list.items = temp;
    }
    // Duplicate the file path and add to the list
    file_list.items[file_list.count++] = strdup(fpath);
    return 0;  // Return 0 to indicate success
}

// Comparison function for qsort()
// Compares two file paths for sorting using locale-specific collation.
// Parameters: const void *a, const void *b - pointers to the file paths to compare
// Returns: int - result of strcoll() function
static int compare_func(const void *a, const void *b) {
    const char *pa = *(const char **)a;
    const char *pb = *(const char **)b;
    return strcoll(pa, pb);  // Compare using locale-specific collation
}

// Function to sort and print the file list
// Sorts the file list using qsort() and prints each file path.
// Parameters: None
// Returns: None
static void sort_and_print() {
    setlocale(LC_COLLATE, "");  // Set locale for collation
    qsort(file_list.items, file_list.count, sizeof(char *), compare_func);
    // Print each file path
    for (size_t i = 0; i < file_list.count; i++) {
        puts(file_list.items[i]);
        free(file_list.items[i]);  // Free the duplicated string
    }
}

// Function to free the file list memory
// Frees the memory allocated for the file list.
// Parameters: None
// Returns: None
static void free_file_list() {
    free(file_list.items);
}

// Callback function for nftw()
// Processes each entry during directory traversal. Determines the type of entry and calls the output function if it matches the flags.
// Parameters: const char *fpath - the path of the current entry
//             const struct stat *sb - pointer to the stat structure of the current entry
//             int typeflag - type of the current entry (e.g., file, directory, etc.)
//             struct FTW *ftwbuf - pointer to the FTW structure with additional information
// Returns: int - 0 to continue traversal, non-zero to stop traversal
static int process_entry(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
    (void) sb;
    (void) ftwbuf;
    int print = 0;  // Flag to determine if the current entry should be printed

    // Determine the type of the current entry and check against the flags
    switch (typeflag) {
        case FTW_SL: // Symbolic link
        case FTW_SLN:
            if (flags & FLAG_LINKS) print = 1;
            break;
        case FTW_D:  // Directory
            if (flags & FLAG_DIRS) print = 1;
            break;
        case FTW_F:  // Regular file
            if (flags & FLAG_FILES) print = 1;
            break;
        default:
            break;  // Other types are ignored
    }

    if (print) {
        // Call the output function (either puts or add_to_list)
        if (output_function(fpath) != 0) {
            // Handle error but continue traversal
            fprintf(stderr, "Error processing '%s'\n", fpath);
        }
    }

    return 0;  // Return 0 to continue traversal
}

// Function to perform directory traversal
// Takes a directory path and traversal flags. Traverses the directory and processes each entry using nftw().
// Parameters: const char *path - the path of the directory to traverse
//             int dirwalk_flags - flags to control the traversal behavior
// Returns: None
void dirwalk(const char *path, int dirwalk_flags) {
    flags = dirwalk_flags;  // Set the global flags

    // Determine the output function based on whether sorting is required
    if (flags & FLAG_SORT) {
        // If sorting is required, initialize the file list
        init_file_list();
        output_function = add_to_list;  // Collect entries for later sorting and printing
    } else {
        output_function = puts;  // Print entries immediately
    }

    // Start the directory traversal using nftw()
    if (nftw(path, process_entry, 20, FTW_PHYS) == -1) {
        fprintf(stderr, "nftw error on '%s': %s\n", path, strerror(errno));
        exit(EXIT_FAILURE);
    }

    // If sorting was required, sort and print the collected entries
    if (flags & FLAG_SORT) {
        sort_and_print();
        free_file_list();  // Free allocated memory
    }
}

