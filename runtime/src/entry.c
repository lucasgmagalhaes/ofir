// Process entry: runs the program and flushes standard output.

#include "ofir_rt.h"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <windows.h>
#endif

int main(int argc, char **argv) {
    int top;   // the main thread's stack starts about here
#ifdef _WIN32
    // Bytes in and out as they are: no `\r\n` for `\n`.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
    SetConsoleOutputCP(CP_UTF8);   // the console shows the UTF-8 the program writes
#endif
    ofir_stack_guard_init(&top);
    ofir_set_args(argc, argv);
    ofir_main();
    ofir_stdout_flush();
    return 0;
}
