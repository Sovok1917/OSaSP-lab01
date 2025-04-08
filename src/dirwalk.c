/**
 * @file dirwalk.c
 * @brief Scans a directory tree and lists files, directories, or symbolic links.
 *
 * This program recursively scans a specified directory (or the current directory
 * if none is provided) and prints the paths of filesystem entries to standard
 * output, similar to the 'find' utility. Options allow filtering by type
 * (file, directory, symbolic link) and sorting the output. Adheres to POSIX
 * standards and specific coding style requirements. Handles unreadable
 * directories similarly to 'find'.
 */

#define _XOPEN_SOURCE 700 // Enable POSIX features including nftw

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // For getopt
#include <getopt.h> // For getopt
#include <sys/stat.h>
#include <ftw.h>    // For nftw
#include <errno.h>
#include <locale.h> // For strcoll, setlocale
#include <stdbool.h> // For bool type (C99+)

// --- Configuration and Constants ---

#define MAX_FTW_FD 20 // Max file descriptors for nftw

// --- Type Definitions ---

/**
 * @brief Structure to hold the list of paths found during traversal when sorting.
 */
typedef struct {
    char **paths;    // Dynamically allocated array of path strings
    size_t count;    // Number of paths currently stored
    size_t capacity; // Allocated capacity of the paths array
} path_list_s;

/**
 * @brief Structure to hold program options.
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
// Used to pass options to the nftw callback function, as standard POSIX nftw
// doesn't support a user data passthrough argument.
static options_s *g_callback_options = NULL;

// --- Function Prototypes ---

static int process_entry(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf);
static int add_path_to_list(path_list_s *list, const char *path);
static void free_path_list(path_list_s *list);
static int compare_paths(const void *a, const void *b);
static void print_usage(const char *prog_name);

// --- Main Function ---

/**
 * @brief Main entry point of the dirwalk program.
 *
 * Parses command-line arguments, sets up options, and initiates the
 * directory traversal using nftw. Handles printing or sorting/printing
 * the results based on options.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return EXIT_SUCCESS on success, EXIT_FAILURE on error.
 */
int main(int argc, char *argv[]) {
    options_s opts = {0}; // Initialize all options to false/NULL
    char *start_dir = "."; // Default starting directory
    int opt;

    // Set locale for sorting according to environment
    if (setlocale(LC_COLLATE, "") == NULL) {
        fprintf(stderr, "Warning: Failed to set locale, sorting may be incorrect.\n");
        // Continue execution even if locale setting fails
    }

    // --- Argument Parsing ---
    optind = 1; // Reset getopt index

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
            case '?': // Unknown option or missing argument for an option
                fprintf(stderr, "Error: Invalid option or missing argument.\n");
                print_usage(argv[0]);
                return EXIT_FAILURE;
            default:
                // Should not happen with the given optstring
                fprintf(stderr, "Internal error: unexpected getopt result '%c'\n", opt);
                abort(); // Use abort for unexpected internal errors
        }
    }

    // --- Determine Starting Directory ---
    if (optind < argc) {
        // Check if more than one non-option argument is provided
        if (optind + 1 < argc) {
            fprintf(stderr, "Error: Too many directory arguments provided.\n");
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
        start_dir = argv[optind];
    } else {
        // No directory argument provided, use default "."
        // If argc == 1 (only program name), print usage hint
        if (argc == 1) {
            print_usage(argv[0]);
            return EXIT_SUCCESS; // Exit successfully after showing help
        }
        // If options were given but no dir, still use "."
    }


    // If no filter options were given, show all types
    if (!opts.filter_active) {
        opts.show_links = true;
        opts.show_dirs = true;
        opts.show_files = true;
    }

    // --- Perform Directory Walk ---
    path_list_s results_list = {0}; // Initialize list for sorting

    if (opts.sort_output) {
        // Initialize the list
        results_list.capacity = 100; // Sensible initial capacity
        results_list.paths = malloc(results_list.capacity * sizeof(char *));
        if (results_list.paths == NULL) {
            perror("Error allocating initial path list");
            return EXIT_FAILURE;
        }
        results_list.count = 0;
        opts.list = &results_list; // Point options struct to the list
    } else {
        opts.list = NULL; // Not sorting, list not needed in options struct
    }

    // Assign the address of local opts struct to the global pointer for callback access
    g_callback_options = &opts;

    // Use FTW_PHYS to not follow symbolic links by default (lstat behavior)
    int flags = FTW_PHYS;

    // Call nftw with the correct 4 arguments for POSIX standard
    if (nftw(start_dir, process_entry, MAX_FTW_FD, flags) == -1) {
        // nftw sets errno, check if it's just a non-existent start directory or permission issue
        fprintf(stderr, "Error walking directory '%s': %s\n", start_dir, strerror(errno));
        // Cleanup potentially allocated sort list even on failure
        if (opts.sort_output) {
            free_path_list(&results_list);
        }
        g_callback_options = NULL; // Reset global pointer
        return EXIT_FAILURE;
    }

    // Reset global pointer after nftw finishes or fails. Good practice.
    g_callback_options = NULL;

    // --- Output Results ---
    if (opts.sort_output) {
        // Sort the collected paths using locale-aware comparison
        qsort(results_list.paths, results_list.count, sizeof(char *), compare_paths);

        // Print the sorted paths
        for (size_t i = 0; i < results_list.count; ++i) {
            // Check for potential write errors during printing
            if (printf("%s\n", results_list.paths[i]) < 0) {
                perror("Error writing to stdout");
                // Cleanup is important even if printing fails partially
                free_path_list(&results_list);
                return EXIT_FAILURE; // Exit on write error
            }
        }

        // Free allocated memory for the list
        free_path_list(&results_list);
    }
    // If not sorting, output happened directly within process_entry

    // Ensure stdout is flushed before exiting
    if (fflush(stdout) == EOF) {
        perror("Error flushing stdout");
        // Decide if this is a fatal error. Often ignored, but good to check.
    }

    return EXIT_SUCCESS;
}

// --- Helper Functions ---

/**
 * @brief Callback function for nftw.
 *
 * This function is called by nftw for each entry found in the directory tree.
 * It checks if the entry type matches the requested types based on the options
 * accessed via the file-scope 'g_callback_options' pointer. Handles special
 * directory types like FTW_DNR to better match 'find' behavior. If sorting is
 * enabled, it adds the path to a list; otherwise, it prints the path directly.
 * Uses the stat buffer to precisely identify regular files when -f is active.
 * When no type filter is active, it lists all encountered entry types similar to find.
 *
 * @param fpath The full path to the current filesystem entry.
 * @param sb Pointer to the stat buffer (from lstat due to FTW_PHYS). Used for precise type checking.
 * @param typeflag An integer indicating the type of the entry (e.g., FTW_F, FTW_D, FTW_SL, FTW_DNR).
 * @param ftwbuf Pointer to the FTW structure containing depth information. Unused here.
 * @return 0 to continue the walk, non-zero to stop. Returns -1 on allocation or print error.
 */
static int process_entry(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
    (void)ftwbuf; // Mark unused

    options_s *opts = g_callback_options;
    if (opts == NULL) {
        fprintf(stderr, "Internal error: callback options pointer is NULL.\n");
        return -1; // Stop walk
    }

    // Always ignore these nftw flags for matching find's output behavior
    if (typeflag == FTW_NS || typeflag == FTW_DP) {
        return 0; // Continue walk, but don't process this entry
    }
    // Double check sb isn't NULL if typeflag wasn't FTW_NS, though nftw docs
    // suggest sb is valid for all flags we care about here if FTW_NS didn't occur.
    // Using sb is only strictly needed for the S_ISREG check when -f is active.

    bool match = false;

    // --- Determine if the entry should be listed ---
    if (!opts->filter_active) {
        // No specific filters (-l, -d, -f) were given. List *everything*
        // encountered by nftw (that isn't FTW_NS or FTW_DP). This includes
        // regular files, dirs, links, sockets, FIFOs, devices, etc.
        // FTW_DNR (unreadable dirs) are included here implicitly.
        match = true;
    } else {
        // Specific filters *are* active. Apply the filtering logic.
        // Symbolic Link: Can be FTW_SL (points somewhere) or FTW_SLN (dangling link)
        if (opts->show_links && (typeflag == FTW_SL || typeflag == FTW_SLN)) {
            match = true;
        }
        // Directory: Can be FTW_D (visiting before contents) or FTW_DNR (cannot read contents)
        else if (opts->show_dirs && (typeflag == FTW_D || typeflag == FTW_DNR)) {
            match = true;
        }
        // File: Must be a *regular* file. Use S_ISREG. sb must be valid here.
        // The check typeflag == FTW_F ensures it's some kind of file/device stat succeeded on.
        else if (opts->show_files && typeflag == FTW_F) {
            // Only perform the S_ISREG check if sb is actually available.
            // While FTW_NS should catch most stat failures, defensive check doesn't hurt,
            // although standard nftw guarantees sb for FTW_F.
            if (sb != NULL && S_ISREG(sb->st_mode)) {
                match = true;
            }
            // If sb were somehow NULL here despite typeflag==FTW_F, it wouldn't match.
        }
        // Other types (sockets, fifos, devices etc.) are implicitly ignored
        // by these specific checks when a filter IS active.
    }

    // --- Process the matched entry ---
    if (match) {
        if (opts->sort_output) {
            // Add path to the list for later sorting
            if (opts->list == NULL) { // Safety check
                fprintf(stderr, "Internal error: sort list pointer is NULL in callback.\n");
                return -1; // Stop walk
            }
            if (add_path_to_list(opts->list, fpath) != 0) {
                // Error message printed inside add_path_to_list
                return -1; // Stop the walk due to allocation error
            }
        } else {
            // Print path directly to standard output
            if (printf("%s\n", fpath) < 0) {
                if (errno == EPIPE) {
                    // Handle broken pipe silently or just stop
                    return -1; // Stop walk silently on EPIPE
                } else {
                    perror("Error writing to stdout");
                }
                // Stop walk on any other stdout error.
                return -1;
            }
        }
    }

    // Return 0 to tell nftw to continue the walk.
    return 0;
}

/**
 * @brief Adds a copy of a path string to the dynamic path list.
 *
 * Resizes the list's internal buffer using realloc if necessary. Handles memory
 * allocation errors robustly.
 *
 * @param list Pointer to the path_list_s structure. Must be non-NULL and initialized.
 * @param path The path string to add. A copy will be made using strdup.
 * @return 0 on success, -1 on memory allocation failure.
 */
static int add_path_to_list(path_list_s *list, const char *path) {
    // Basic sanity check
    if (list == NULL) {
        fprintf(stderr, "Internal error: add_path_to_list called with NULL list.\n");
        return -1;
    }

    // Check if capacity needs to be increased
    if (list->count >= list->capacity) {
        // Double the capacity, or start with a base capacity if it's 0.
        size_t new_capacity = (list->capacity == 0) ? 100 : list->capacity * 2;
        // Check for potential integer overflow if capacity becomes extremely large
        if (new_capacity <= list->capacity && list->capacity > 0) {
            fprintf(stderr, "Error: Path list capacity overflow during resize.\n");
            return -1; // Indicate error
        }
        // Attempt to reallocate. realloc(NULL, size) is equivalent to malloc(size).
        char **new_paths = realloc(list->paths, new_capacity * sizeof(char *));
        if (new_paths == NULL) {
            perror("Error reallocating path list buffer");
            // The original list->paths is still valid if realloc fails. The walk should stop.
            return -1; // Indicate error
        }
        list->paths = new_paths;
        list->capacity = new_capacity;
    }

    // Duplicate the path string using strdup (allocates memory for the copy)
    list->paths[list->count] = strdup(path);
    if (list->paths[list->count] == NULL) {
        perror("Error duplicating path string (strdup failed)");
        // If strdup fails, the list count hasn't been incremented yet.
        // The walk should stop. Any previously added paths will be freed later.
        return -1; // Indicate error
    }

    // Increment count only after successful allocation and copy
    list->count++;
    return 0; // Indicate success
}


/**
 * @brief Frees all memory associated with a path list.
 *
 * Frees each individual path string allocated by strdup and then frees the
 * array holding the pointers. Resets the list structure members to safe values.
 *
 * @param list Pointer to the path_list_s structure to free. Handles NULL input safely.
 */
static void free_path_list(path_list_s *list) {
    if (list == NULL || list->paths == NULL) {
        // Nothing to free if list or paths array is NULL
        return;
    }
    // Free each individual path string that was strdup'd
    for (size_t i = 0; i < list->count; ++i) {
        free(list->paths[i]);
        // Setting to NULL after free isn't strictly necessary here as the whole
        // array is freed next, but can be good practice in other contexts.
        // list->paths[i] = NULL;
    }
    // Free the array of pointers itself
    free(list->paths);

    // Reset the struct members to prevent dangling pointers or incorrect state
    list->paths = NULL;
    list->count = 0;
    list->capacity = 0;
}

/**
 * @brief Comparison function for qsort, using locale-aware string comparison.
 *
 * Used to sort path strings according to the current LC_COLLATE setting,
 * ensuring correct alphabetical order for the user's locale.
 *
 * @param a Pointer to the first element being compared (const void * -> const char **).
 * @param b Pointer to the second element being compared (const void * -> const char **).
 * @return An integer less than, equal to, or greater than zero if the string pointed
 *         to by 'a' is found, respectively, to be less than, to match, or be
 *         greater than the string pointed to by 'b' according to strcoll.
 */
static int compare_paths(const void *a, const void *b) {
    // qsort passes pointers to the elements being sorted.
    // Since we are sorting an array of (char *), a and b are (const char **).
    const char **path_a_ptr = (const char **)a;
    const char **path_b_ptr = (const char **)b;

    // Dereference the pointers to get the actual (const char *) strings
    // Use strcoll for locale-sensitive comparison.
    return strcoll(*path_a_ptr, *path_b_ptr);
}


/**
 * @brief Prints usage instructions for the program to standard error.
 *
 * Includes the program name, expected arguments, and available options.
 *
 * @param prog_name The name of the program executable (typically argv[0]).
 */
static void print_usage(const char *prog_name) {
    // Use fprintf to stderr for usage and error messages, as is conventional.
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
