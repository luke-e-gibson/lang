#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <filesystem>

#include "repl.h"
#include "parser.h"
#include "codegen.h"
#include "emit.h"
#include "jit.h"
#include "source_map.h"

namespace fs = std::filesystem;

static bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

static void usage() {
    printf("usage: compiler [file.lang] [-o out.exe] [--run] [--emit-ir] [--emit-obj]\n"
           "       compiler            (no arguments starts the REPL)\n");
}

// Converts a byte offset to "line:col" for readable errors.
static std::string lineCol(const std::string& src, uint32_t off) {
    uint32_t line = 1, col = 1;
    for (uint32_t i = 0; i < off && i < src.size(); ++i) {
        if (src[i] == '\n') { ++line; col = 1; } else ++col;
    }
    return std::to_string(line) + ":" + std::to_string(col);
}

static int compileFile(const std::string& input, std::string output,
                       bool run, bool emitIR, bool emitObj) {
    std::string src;
    if (!readFile(input, src)) {
        fprintf(stderr, "error: cannot read '%s'\n", input.c_str());
        return 1;
    }

    SourceMap sm(input, src);

    // `src` must outlive the parser and every token (they hold views into it).
    Parser parser(src);
    Program prog = parser.parseProgram();
    
    bool failed = false;
    for (auto& d : parser.lexDiagnostics()) { fputs(sm.format(d.offset, d.message).c_str(), stderr); failed = true; }
    for (auto& d : parser.errors()) { fputs(sm.format(d.offset, d.message).c_str(), stderr); failed = true; }
    if (failed) return 1;

    CodeGen cg;
    if (!cg.generate(prog)) {
       for (auto& d : cg.errors()) fputs(sm.format(d.offset, d.message).c_str(), stderr);
        return 1;
    }
    for (auto& d : cg.errors()) fputs(sm.format(d.offset, d.message).c_str(), stderr);

    if (emitIR) { cg.print(); return 0; }

    fs::path in(input);
    fs::path exePath = output.empty() ? in.stem().string() + ".exe" : output;
    fs::path objPath = exePath;
    objPath.replace_extension(".obj");

    std::string err;
    if (!emitObjectFile(cg.module(), objPath.string(), err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    if (emitObj) { printf("wrote %s\n", objPath.string().c_str()); return 0; }

    // Needs link.exe on PATH, which a Developer Command Prompt provides.
    std::string cmd = "link /NOLOGO /SUBSYSTEM:CONSOLE \"/OUT:" + exePath.string() + "\" \"" +
                      objPath.string() +
                      "\" /DEFAULTLIB:libcmt /DEFAULTLIB:legacy_stdio_definitions";
    int rc = std::system(("\"" + cmd + "\"").c_str());
    fs::remove(objPath);
    if (rc != 0) {
        fprintf(stderr, "error: linking failed (is link.exe on PATH? "
                        "use a Developer Command Prompt)\n");
        return 1;
    }
    printf("wrote %s\n", exePath.string().c_str());

    if (run) {
        std::string runCmd = "\"" + fs::absolute(exePath).string() + "\"";
        return std::system(("\"" + runCmd + "\"").c_str());
    }
    return 0;
}

int main(int argc, char** argv) {
    jitInit();   // register native target once (needed by the JIT and emitObjectFile)

    if (argc == 1) {
        REPL repl;
        repl.Run();
        return 0;
    }

    std::string input, output;
    bool run = false, emitIR = false, emitObj = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-o" && i + 1 < argc)  output = argv[++i];
        else if (a == "--run")          run = true;
        else if (a == "--emit-ir")      emitIR = true;
        else if (a == "--emit-obj")     emitObj = true;
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else if (!a.empty() && a[0] == '-') {
            fprintf(stderr, "unknown option '%s'\n", a.c_str());
            usage();
            return 1;
        }
        else input = a;
    }

    if (input.empty()) { usage(); return 1; }
    return compileFile(input, output, run, emitIR, emitObj);
}