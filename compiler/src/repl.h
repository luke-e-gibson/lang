#pragma once
#include <vector>

class REPL {
public:
    REPL();
    void Run();

private:
    bool running = true;
    std::vector<char> inputBuffer;
};