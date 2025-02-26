// dirwalk.c
#define _XOPEN_SOURCE 500  // Required for nftw
#define _POSIX_C_SOURCE 200809L

#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <errno.h>
#include "dirwalk.h"

static int flags;
static int add_to_list(const char *fpath);
static int (*output_function)(const char *fpath);

typedef struct {
    char **items;
    size_t capacity;
    size_t count;
} FileList;

static FileList file_list = {NULL, 0, 0};

// Initialize the file list
static void init_file_list() {
    file_list.capacity = 100;
    file_list.count = 0;
    file_list.items = malloc(file_list.capacity * sizeof(char *));
    if (!file_list.items) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
}

// Add a file path to the list
static int add_to_list(const char *fpath) {
    if (file_list.count >= file_list.capacity) {
        file_list.capacity *= 2;
        char **temp = realloc(file_list.items, file_list.capacity * sizeof(char *));
        if (!temp) {
            perror("realloc");
            return 1; // Signal to stop traversal
        }
        file_list.items = temp;
    }
    file_list.items[file_list.count++] = strdup(fpath);
    return 0;
}

// Compare function for qsort
static int compare_func(const void *a, const void *b) {
    const char *pa = *(const char **)a;
    const char *pb = *(const char **)b;
    return strcoll(pa, pb);
}

// Sort and print the file list
static void sort_and_print() {
    setlocale(LC_COLLATE, "");
    qsort(file_list.items, file_list.count, sizeof(char *), compare_func);
    for (size_t i = 0; i < file_list.count; i++) {
        puts(file_list.items[i]);
        free(file_list.items[i]);
    }
}

// Free the file list
static void free_file_list() {
    free(file_list.items);
}

// Function to process each file/directory
static int process_entry(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
    int print = 0;

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
            break;
    }

    if (print) {
        if (output_function(fpath) != 0) {
            fprintf(stderr, "Error processing '%s'\n", fpath);
            // Continue traversal even if there's an error
        }
    }

    return 0; // Always return 0 to continue traversal
}

// Function to walk the directory tree
void dirwalk(const char *path, int dirwalk_flags) {
    flags = dirwalk_flags;

    if (flags & FLAG_SORT) {
        // If sorting is required, collect entries
        init_file_list();
        output_function = add_to_list;
    } else {
        // Otherwise, print immediately
        output_function = puts;
    }

    if (nftw(path, process_entry, 20, FTW_PHYS) == -1) {
        fprintf(stderr, "nftw error on '%s': %s\n", path, strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (flags & FLAG_SORT) {
        sort_and_print();
        free_file_list();
    }
}

