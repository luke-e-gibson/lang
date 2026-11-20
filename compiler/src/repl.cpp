#include "repl.h"
#include "parser.h"
#include <stdio.h>

REPL::REPL() {}

void REPL::Run() {
    while (running) {
        printf("compiler> ");
        inputBuffer.clear();
        int c;
        while ((c = getchar()) != '\n' && c != EOF) {
            inputBuffer.push_back(static_cast<char>(c));
        }
        if (c == EOF) {
            running = false;
            continue;
        }

        Parser parser(std::string_view(inputBuffer.data(), inputBuffer.size()));
        Program prog = parser.parseProgram();

        for (auto& e : parser.errors()) printf("%s\n", e.c_str());
        for (auto& d : parser.lexDiagnostics())
            printf("lex error at %u: %s\n", d.offset, d.message.c_str());

        for (auto& s : prog.items) dumpStmt(*s, 0);
    }
}