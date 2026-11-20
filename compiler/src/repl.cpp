#include "repl.h"
#include <stdio.h>

REPL::REPL() {}

void REPL::Run() {
    while (running) {
        printf("compiler> ");
        inputBuffer.clear();
        char c;
        while ((c = getchar()) != '\n' && c != EOF) {
            inputBuffer.push_back(c);
        }
        if (c == EOF) {
            running = false;
            continue;
        }
        inputBuffer.push_back('\0');
        std::printf("You entered: %s\n", inputBuffer.data());
    }
}
