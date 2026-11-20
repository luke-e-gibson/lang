#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <filesystem>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "compiler_config.h"
#include "repl.h"
#include "parser.h"
#include "codegen.h"
#include "emit.h"
#include "jit.h"
#include "source_map.h"

namespace fs = std::filesystem;

#ifdef _WIN32
static std::wstring toWide(const std::string& value) {
    if (value.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, result.data(), size);
    if (!result.empty() && result.back() == L'\0') result.pop_back();
    return result;
}

static std::wstring windowsQuote(const std::string& value) {
    std::wstring result = L"\"";
    std::wstring wide = toWide(value);
    size_t backslashes = 0;
    for (wchar_t c : wide) {
        if (c == L'\\') {
            ++backslashes;
        } else {
            if (c == L'"') result.append(backslashes * 2 + 1, L'\\');
            else result.append(backslashes, L'\\');
            backslashes = 0;
            result += c;
        }
    }
    result.append(backslashes, L'\\');
    result += L'"';
    return result;
}

static int runWindows(const std::string& executable,
                      const std::wstring& arguments) {
    std::wstring application = toWide(executable);
    std::wstring commandLine = windowsQuote(executable) + L" " + arguments;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(application.c_str(), commandLine.data(), nullptr, nullptr,
                        FALSE, 0, nullptr, nullptr, &startup, &process)) {
        return 1;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
    return static_cast<int>(exitCode);
}
#endif

static bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

static void usage() {
    printf("usage: compiler [file.lang] [-o output] [--run] [--emit-ir] [--emit-obj]\n"
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
    fs::path exePath = output.empty()
        ? in.stem().string() + LANG_EXECUTABLE_SUFFIX
        : output;
    fs::path objPath = exePath;
    objPath.replace_extension(LANG_OBJECT_SUFFIX);

    std::string err;
    if (!emitObjectFile(cg.module(), objPath.string(), err)) {
        fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    if (emitObj) { printf("wrote %s\n", objPath.string().c_str()); return 0; }

    // Let the compiler driver select the native linker and runtime libraries.
#ifdef _WIN32
    std::wstring arguments = windowsQuote(objPath.string()) + L" " +
                             toWide(LANG_LINK_OUTPUT_OPTION) +
                             windowsQuote(exePath.string()) + L" " +
                             toWide(LANG_LINK_FLAGS);
    int rc = runWindows(LANG_CXX_COMPILER, arguments);
#else
    std::string cmd = shellQuote(LANG_CXX_COMPILER) + " " +
                      shellQuote(objPath.string()) + " " +
                      LANG_LINK_OUTPUT_OPTION;
    if (LANG_LINK_OUTPUT_NEEDS_SPACE) cmd += " ";
    cmd += shellQuote(exePath.string());
    int rc = std::system(cmd.c_str());
#endif
    fs::remove(objPath);
    if (rc != 0) {
        fprintf(stderr, "error: linking failed (is the configured C++ compiler available?)\n");
        return 1;
    }
    printf("wrote %s\n", exePath.string().c_str());

    if (run) {
#ifdef _WIN32
        fs::path absoluteExe = fs::absolute(exePath);
        return runWindows(absoluteExe.string(), L"");
#else
        return std::system(shellQuote(fs::absolute(exePath).string()).c_str());
#endif
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