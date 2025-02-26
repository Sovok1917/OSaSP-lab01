#ifndef DIRWALK_H
#define DIRWALK_H

// Flags for option handling
#define FLAG_LINKS  (1 << 0)
#define FLAG_DIRS   (1 << 1)
#define FLAG_FILES  (1 << 2)
#define FLAG_SORT   (1 << 3)

// Function prototype
void dirwalk(const char *path, int dirwalk_flags);

#endif // DIRWALK_H
