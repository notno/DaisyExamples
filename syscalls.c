// syscalls.c

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/unistd.h>

// Provide implementations for system calls
// These are minimal stubs suitable for bare-metal systems

// _sbrk: Memory allocation for heap
caddr_t _sbrk(int incr) {
    extern char _end; // Defined by the linker
    static char *heap_end = NULL;
    char *prev_heap_end;

    if (heap_end == NULL) {
        heap_end = &_end;
    }
    prev_heap_end = heap_end;
    heap_end += incr;

    return (caddr_t) prev_heap_end;
}

// _write: Handle write calls (e.g., for printf)
int _write(int file, char *ptr, int len) {
    // Implement as needed, or leave as a stub
    // For example, you can redirect to UART or another output
    return len;
}

// _read: Handle read calls
int _read(int file, char *ptr, int len) {
    // Implement as needed, or leave as a stub
    return 0;
}

// _close: Handle close calls
int _close(int file) {
    return -1;
}

// _fstat: Handle fstat calls
int _fstat(int file, struct stat *st) {
    st->st_mode = S_IFCHR;
    return 0;
}

// _isatty: Handle isatty calls
int _isatty(int file) {
    return 1;
}

// _lseek: Handle lseek calls
int _lseek(int file, int ptr, int dir) {
    return 0;
}

// _exit: Handle exit calls
void _exit(int status) {
    while (1);
}
