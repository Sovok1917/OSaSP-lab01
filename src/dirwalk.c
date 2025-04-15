/*
 * dirwalk.c
 *
 * Description:
 * This program scans a directory tree, starting from a specified path
 * (or the current directory by default), and lists filesystem entries
 * such as files, directories, and symbolic links. It mimics some basic
 * functionality of the 'find' command-line utility.
 *
 * Features:
 * - Recursive directory traversal using POSIX nftw().
 * - Optional filtering to show only files (-f), directories (-d), or
 *   symbolic links (-l). If no filter is specified, all types are shown.
 * - Optional sorting (-s) of the output paths alphabetically based on the
 *   current locale settings (LC_COLLATE).
 * - Handles unreadable directories gracefully, similar to 'find'.
 * - Adheres to POSIX standards (_XOPEN_SOURCE 700).
 */

#define _XOPEN_SOURCE 700 // Enable POSIX features including nftw

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/stat.h>
#include <ftw.h>
#include <errno.h>
#include <locale.h> // For strcoll, setlocale
#include <stdbool.h> // For bool type (C99+)

// --- Configuration and Constants ---

#define MAX_FTW_FD 20 // Max file descriptors for nftw

// --- Type Definitions ---

/*
 * Structure to hold the list of paths found during traversal,
 * used specifically when sorting is enabled.
 */
typedef struct {
    char **paths;    // Dynamically allocated array of path strings
    size_t count;    // Number of paths currently stored
    size_t capacity; // Allocated capacity of the paths array
} path_list_s;

/*
 * Structure to hold the command-line options parsed by the program.
 */
typedef struct {
    bool show_links;     // True if -l is specified
    bool show_dirs;      // True if -d is specified
    bool show_files;     // True if -f is specified
    bool sort_output;    // True if -s is specified
    bool filter_active;  // True if any of -l, -d, or -f were specified
    path_list_s *list;   // Pointer to path list (used only if sort_output is true)
} options_s;

// --- File-Scope Static Variable ---
// Used to pass options to the nftw callback function (process_entry).
// This is a common workaround as standard POSIX nftw doesn't provide a
// user-defined data pointer argument for the callback.
static options_s *g_callback_options = NULL;

// --- Function Prototypes ---

static int process_entry(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf);
static int add_path_to_list(path_list_s *list, const char *path);
static void free_path_list(path_list_s *list);
static int compare_paths(const void *a, const void *b);
static void print_usage(const char *prog_name);

// --- Main Function ---

/*
 * Purpose:
 *   Main entry point of the dirwalk program. It parses command-line arguments,
 *   sets up the options structure, initiates the directory traversal using nftw(),
 *   and handles the output (either printing directly or collecting, sorting,
 *   and then printing).
 * Receives:
 *   argc: The number of command-line arguments.
 *   argv: An array of command-line argument strings.
 * Returns:
 *   EXIT_SUCCESS (0) if the program executes successfully.
 *   EXIT_FAILURE (1) if an error occurs (e.g., invalid options, memory
 *   allocation failure, directory traversal error).
 */
int main(int argc, char *argv[]) {
    options_s opts = {0};
    char *start_dir = ".";
    int opt;

    // Set locale for sorting based on environment variables.
    if (setlocale(LC_COLLATE, "") == NULL) {
        fprintf(stderr, "Warning: Failed to set locale, sorting may be incorrect.\n");
        // Continue even if locale setting fails.
    }

    optind = 1; // Ensure getopt starts from the first argument.

    // Parse command-line options.
    while ((opt = getopt(argc, argv, "ldfs")) != -1) {
        switch (opt) {
            case 'l':
                opts.show_links = true;
                opts.filter_active = true;
                break;
            case 'd':
                opts.show_dirs = true;
                opts.filter_active = true;
                break;
            case 'f':
                opts.show_files = true;
                opts.filter_active = true;
                break;
            case 's':
                opts.sort_output = true;
                break;
            case '?': // Handle unknown options or missing arguments.
                fprintf(stderr, "Error: Invalid option or missing argument.\n");
                print_usage(argv[0]);
                return EXIT_FAILURE;
            default:
                // This case should ideally not be reached with the defined optstring.
                fprintf(stderr, "Internal error: unexpected getopt result '%c'\n", opt);
                abort();
        }
    }

    // Determine the starting directory from non-option arguments.
    if (optind < argc) {
        // Check for too many non-option arguments.
        if (optind + 1 < argc) {
            fprintf(stderr, "Error: Too many directory arguments provided.\n");
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
        start_dir = argv[optind];
    } else {
        // No directory specified, use default ".".
        // If only the program name was given, show usage.
        if (argc == 1) {
            print_usage(argv[0]);
            return EXIT_SUCCESS; // Exit normally after showing help.
        }
        // If options were given but no directory, still use ".".
    }


    // If no specific type filters were activated, default to showing all types.
    if (!opts.filter_active) {
        opts.show_links = true;
        opts.show_dirs = true;
        opts.show_files = true;
    }

    // Prepare for directory walk.
    path_list_s results_list = {0}; // Initialize list structure for sorting.

    if (opts.sort_output) {
        // Allocate initial memory for the path list if sorting is enabled.
        results_list.capacity = 100; // Sensible initial capacity.
        results_list.paths = malloc(results_list.capacity * sizeof(char *));
        if (results_list.paths == NULL) {
            perror("Error allocating initial path list");
            return EXIT_FAILURE;
        }
        results_list.count = 0;
        opts.list = &results_list; // Link the list to the options structure.
    } else {
        opts.list = NULL; // No list needed if not sorting.
    }

    // Make options accessible to the callback function via the global pointer.
    g_callback_options = &opts;

    // Use FTW_PHYS to make nftw behave like lstat (doesn't follow symlinks during traversal).
    int flags = FTW_PHYS;

    // Perform the directory traversal.
    if (nftw(start_dir, process_entry, MAX_FTW_FD, flags) == -1) {
        fprintf(stderr, "Error walking directory '%s': %s\n", start_dir, strerror(errno));
        // Ensure cleanup even if nftw fails.
        if (opts.sort_output) {
            free_path_list(&results_list);
        }
        g_callback_options = NULL; // Reset global pointer.
        return EXIT_FAILURE;
    }

    // Reset global pointer after nftw finishes or fails.
    g_callback_options = NULL;

    // Process and output the results.
    if (opts.sort_output) {
        // Sort the collected paths using locale-aware comparison.
        qsort(results_list.paths, results_list.count, sizeof(char *), compare_paths);

        // Print the sorted paths.
        for (size_t i = 0; i < results_list.count; ++i) {
            if (printf("%s\n", results_list.paths[i]) < 0) {
                perror("Error writing to stdout");
                free_path_list(&results_list); // Clean up before exiting on error.
                return EXIT_FAILURE;
            }
        }

        // Free the memory used by the path list.
        free_path_list(&results_list);
    }
    // If not sorting, output happened directly within process_entry.

    // Ensure all buffered output is written.
    if (fflush(stdout) == EOF) {
        perror("Error flushing stdout");
        // Optional: Consider if this should be a fatal error.
    }

    return EXIT_SUCCESS;
}

// --- Helper Functions ---

/*
 * Purpose:
 *   Callback function invoked by nftw() for each filesystem entry encountered
 *   during the directory traversal. It determines if the entry matches the
 *   filtering criteria specified by the command-line options (accessed via
 *   the global g_callback_options). If sorting is disabled, it prints the
 *   path directly. If sorting is enabled, it adds the path to the list
 *   stored in the options structure. It handles different file types reported
 *   by nftw (FTW_F, FTW_D, FTW_SL, FTW_DNR, etc.) and uses the stat buffer
 *   to specifically identify regular files when the -f option is active.
 *   It ignores entries flagged as FTW_NS (stat failed) or FTW_DP (directory
 *   visited post-children - not needed here).
 * Receives:
 *   fpath:    A pointer to a string containing the full path of the current entry.
 *   sb:       A pointer to a 'struct stat' containing information about the entry
 *             (obtained via lstat because FTW_PHYS is used).
 *   typeflag: An integer indicating the type of the entry (e.g., FTW_F, FTW_D, FTW_SL).
 *   ftwbuf:   A pointer to an FTW structure containing depth level and base offset;
 *             this parameter is not used in this function.
 * Returns:
 *   0 to instruct nftw() to continue the traversal.
 *   -1 to instruct nftw() to stop the traversal immediately (e.g., due to an
 *      output error or memory allocation failure).
 */
static int process_entry(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
    (void)ftwbuf; // Indicate ftwbuf is intentionally unused.

    options_s *opts = g_callback_options;
    if (opts == NULL) {
        // This should not happen if main() sets the global pointer correctly.
        fprintf(stderr, "Internal error: callback options pointer is NULL.\n");
        return -1; // Stop the walk.
    }

    // Ignore entries where stat failed or representing directory visit after children.
    if (typeflag == FTW_NS || typeflag == FTW_DP) {
        return 0; // Continue walk, skip processing this entry.
    }

    bool match = false;

    // Determine if the current entry matches the specified filter options.
    if (!opts->filter_active) {
        // If no filters (-l, -d, -f) are active, list everything encountered.
        match = true;
    } else {
        // Apply active filters.
        // Check for symbolic links (includes regular and dangling links).
        if (opts->show_links && (typeflag == FTW_SL || typeflag == FTW_SLN)) {
            match = true;
        }
        // Check for directories (includes readable and unreadable directories).
        else if (opts->show_dirs && (typeflag == FTW_D || typeflag == FTW_DNR)) {
            match = true;
        }
        // Check specifically for *regular* files using stat info.
        else if (opts->show_files && typeflag == FTW_F) {
            // Ensure sb is valid and check if it's a regular file.
            if (sb != NULL && S_ISREG(sb->st_mode)) {
                match = true;
            }
        }
        // Other types (sockets, fifos, devices) are implicitly ignored when filters are active.
    }

    // Process the entry if it matched the criteria.
    if (match) {
        if (opts->sort_output) {
            // Add the path to the list for later sorting.
            if (opts->list == NULL) { // Safety check.
                fprintf(stderr, "Internal error: sort list pointer is NULL in callback.\n");
                return -1; // Stop walk.
            }
            if (add_path_to_list(opts->list, fpath) != 0) {
                // Error message already printed by add_path_to_list.
                return -1; // Stop walk due to allocation error.
            }
        } else {
            // Print the path directly to standard output.
            if (printf("%s\n", fpath) < 0) {
                if (errno == EPIPE) {
                    // Stop silently if writing to a broken pipe.
                    return -1;
                } else {
                    perror("Error writing to stdout");
                    return -1; // Stop on other write errors.
                }
            }
        }
    }

    // Continue the directory traversal.
    return 0;
}

/*
 * Purpose:
 *   Appends a copy of the provided path string to the dynamic path list.
 *   If the list's capacity is reached, it attempts to resize the internal
 *   buffer (usually by doubling its size) using realloc(). It handles memory
 *   allocation for the new path copy using strdup().
 * Receives:
 *   list: A pointer to the path_list_s structure managing the list. Must be
 *         a valid, initialized pointer.
 *   path: The null-terminated string representing the path to add. A duplicate
 *         of this string will be stored.
 * Returns:
 *   0 on success.
 *   -1 if a memory allocation error occurs (either during list resizing with
 *      realloc() or during path duplication with strdup()). An error message
 *      is printed to stderr on failure.
 */
static int add_path_to_list(path_list_s *list, const char *path) {
    if (list == NULL) {
        fprintf(stderr, "Internal error: add_path_to_list called with NULL list.\n");
        return -1;
    }

    // Check if the list needs to grow.
    if (list->count >= list->capacity) {
        size_t new_capacity = (list->capacity == 0) ? 100 : list->capacity * 2;
        // Basic overflow check for capacity.
        if (new_capacity <= list->capacity && list->capacity > 0) {
            fprintf(stderr, "Error: Path list capacity overflow during resize.\n");
            return -1;
        }
        char **new_paths = realloc(list->paths, new_capacity * sizeof(char *));
        if (new_paths == NULL) {
            perror("Error reallocating path list buffer");
            return -1; // The original list->paths remains valid, but we signal failure.
        }
        list->paths = new_paths;
        list->capacity = new_capacity;
    }

    // Duplicate the path string and store the pointer in the list.
    list->paths[list->count] = strdup(path);
    if (list->paths[list->count] == NULL) {
        perror("Error duplicating path string (strdup failed)");
        return -1; // Signal failure.
    }

    // Increment the count only after successful allocation and copy.
    list->count++;
    return 0; // Success.
}


/*
 * Purpose:
 *   Releases all memory dynamically allocated for the path list. This includes
 *   freeing each individual path string (allocated by strdup) and then freeing
 *   the array that holds the pointers to these strings. It also resets the
 *   list's count and capacity members to zero.
 * Receives:
 *   list: A pointer to the path_list_s structure whose memory should be freed.
 *         It is safe to pass NULL or a pointer to a list that hasn't had
 *         memory allocated yet (paths == NULL).
 * Returns:
 *   None (void).
 */
static void free_path_list(path_list_s *list) {
    if (list == NULL || list->paths == NULL) {
        // Nothing allocated to free.
        return;
    }
    // Free each duplicated path string.
    for (size_t i = 0; i < list->count; ++i) {
        free(list->paths[i]);
    }
    // Free the array holding the pointers.
    free(list->paths);

    // Reset members to prevent use after free.
    list->paths = NULL;
    list->count = 0;
    list->capacity = 0;
}

/*
 * Purpose:
 *   Comparison function designed for use with qsort() to sort an array of
 *   path strings (char *). It uses strcoll() for locale-aware string
 *   comparison, ensuring that sorting respects the rules defined by the
 *   current LC_COLLATE environment setting.
 * Receives:
 *   a: A const void pointer to the first element being compared. In the context
 *      of sorting char **, this points to a (char *).
 *   b: A const void pointer to the second element being compared. In the context
 *      of sorting char **, this points to a (char *).
 * Returns:
 *   An integer less than, equal to, or greater than zero if the string referenced
 *   by 'a' is lexicographically less than, equal to, or greater than the string
 *   referenced by 'b', according to strcoll().
 */
static int compare_paths(const void *a, const void *b) {
    // Cast the void pointers to pointers to character pointers (const char **).
    const char **path_a_ptr = (const char **)a;
    const char **path_b_ptr = (const char **)b;

    // Dereference the pointers to get the actual C strings (const char *)
    // and compare them using locale-sensitive strcoll.
    return strcoll(*path_a_ptr, *path_b_ptr);
}


/*
 * Purpose:
 *   Prints usage instructions for the program to the standard error stream (stderr).
 *   It displays the basic command syntax and lists the available command-line options
 *   with brief explanations.
 * Receives:
 *   prog_name: The name of the executable, typically obtained from argv[0].
 * Returns:
 *   None (void).
 */
static void print_usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s [directory] [options]\n", prog_name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  [directory]  Starting directory path (default: current directory './')\n");
    fprintf(stderr, "  -l           List only symbolic links (-type l)\n");
    fprintf(stderr, "  -d           List only directories (-type d)\n");
    fprintf(stderr, "  -f           List only regular files (-type f)\n");
    fprintf(stderr, "  -s           Sort the output alphabetically using LC_COLLATE\n");
    fprintf(stderr, "Details:\n");
    fprintf(stderr, "  If no type options (-l, -d, -f) are given, all entry types are listed.\n");
    fprintf(stderr, "  Options can be combined (e.g., -ls) and can appear before or after the directory path.\n");
    fprintf(stderr, "  Output format matches 'find [dir] -type [opt]' with one path per line.\n");
}
