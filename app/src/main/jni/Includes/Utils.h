#ifndef YAMGG_UTILS_H
#define YAMGG_UTILS_H
#include <unistd.h>
#include <string>
static inline bool isLibraryLoaded(const char* name) {
    FILE* fp = fopen("/proc/self/maps", "r");
    if (!fp) return false;
    char line[512];
    bool found = false;
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, name)) { found = true; break; }
    }
    fclose(fp);
    return found;
}
#endif
