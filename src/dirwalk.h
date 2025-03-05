// dirwalk.h
#ifndef DIRWALK_H
#define DIRWALK_H

// Flags for option handling (used as bitmask values)
#define FLAG_LINKS  (1 << 0)  // 0001: Symbolic links
#define FLAG_DIRS   (1 << 1)  // 0010: Directories
#define FLAG_FILES  (1 << 2)  // 0100: Regular files
#define FLAG_SORT   (1 << 3)  // 1000: Sorting

// Function prototype for dirwalk
void dirwalk(const char *path, int dirwalk_flags);

#endif // DIRWALK_H

