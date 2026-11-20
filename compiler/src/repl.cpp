#include "repl.h"
#include "lexer.h"
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
        Lexer lexer(std::string_view(inputBuffer.data()));
        for(Token token = lexer.next(); token.kind != Tok::Eof; token = lexer.next()) {
            if(token.kind == Tok::Error) {
                printf("Parsing error at position %u: '%.*s'. %s\n", token.offset, (int)token.text.size(), token.text.data(), lexer.error().data());
                break;
            }
            printf("Token: %d, Text: '%.*s', Position: %u\n", (int)token.kind, (int)token.text.size(), token.text.data(), token.offset);
        }
    }
}
