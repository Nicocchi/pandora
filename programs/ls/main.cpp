#include "../libc/stdio.h"
#include "../libc/syscall.h"

#define MAX_ENTRIES 128

int main(int argc, char** argv)
{
    const char* path = (argc > 1) ? argv[1] : "/";

    // Keep entries on the stack: the program image is mapped read-only, so
    // globals/.bss cannot be written.
    DirEntry entries[MAX_ENTRIES];

    long n = sys_readdir(path, entries, MAX_ENTRIES);
    if (n < 0)
    {
        printf("ls: cannot access '");
        printf(path);
        printf("\n");
        return 1;
    }

    for (long i = 0; i < n; i++)
    {
        printf(" %s", entries[i].name);
        if (entries[i].is_dir)
        {
            printf("/\n");
        }
        else
        {
            printf("  (");
            printf("%llu", entries[i].size);
            printf(" bytes)\n");
        }
    }

    return 0;
}
