#include "repl.h"
#include "codegen.h"
#include "jit.h"
#include "parser.h"
#include <stdio.h>

REPL::REPL() {
    jitInit();
}

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

        if (parser.errors().empty()) {
           CodeGen cg;
           if (cg.generate(prog)) {
               // cg.print();   // uncomment to see the IR
               int code = 0;
               std::string err;
               if (!jitRun(cg.takeModule(), code, err))
                   printf("jit error: %s\n", err.c_str());
           } else {
               for (auto& e : cg.errors()) printf("%s\n", e.c_str());
           }
        }
    }
}