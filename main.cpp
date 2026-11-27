//===--- main.cpp - apack, the Axiom bundler -----------------------===//
#include "Packer.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

namespace fs = std::filesystem;

constexpr const char* kVersion = "0.1.0";

void printUsage(std::ostream& out) {
    out << "apack " << kVersion << " - bundles an Axiom program for machines without it\n"
        << "\n"
        << "Usage: apack <file.ax> -o <dir> [options]\n"
        << "\n"
        << "Options:\n"
        << "  -o <dir>          Output directory (required)\n"
        << "  --name <name>     Launcher name, without extension (default: the source's stem)\n"
        << "  --runtime <dir>   Where acvm.exe and the standard library live\n"
        << "                    (default: $ACVM_HOME, else next to this program, else\n"
        << "                    the Axiom build directory it was compiled against)\n"
        << "  -O0 .. -O4        Optimization level (default: -O2)\n"
        << "  --single          One executable instead of a directory: the program is\n"
        << "                    appended to a statically linked runtime, so nothing sits\n"
        << "                    beside it and nothing is extracted at run time\n"
        << "\n"
        << "Identity, stamped into <name>.exe:\n"
        << "  --app-version <v>  File and product version, e.g. 1.2.0 (default: 1.0.0.0)\n"
        << "  --company <text>   Company name\n"
        << "  --copyright <text> Copyright line\n"
        << "  --icon <file.ico>  Application icon\n"
        << "\n"
        << "What it writes: <dir>/<name>.exe, <dir>/<name>.acvm, and every shared\n"
        << "library the program needs. The output is self-contained: it does not\n"
        << "read the toolchain, the standard library sources, or the source file.\n"
        << "With --single, only <dir>/<name>.exe.\n";
}

/// The directory holding this executable.
///
/// Resolved from the module rather than from `argv[0]` because `argv[0]` is whatever the
/// caller typed: a bare `apack` found on `PATH` has no directory in it, and a relative
/// path is relative to the caller's working directory, not to the binary.
fs::path executableDir() {
#if defined(_WIN32)
    wchar_t buffer[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length == MAX_PATH) {
        return {};
    }
    return fs::path(buffer).parent_path();
#else
    std::error_code error;
    const fs::path self = fs::read_symlink("/proc/self/exe", error);
    return error ? fs::path() : self.parent_path();
#endif
}

/// Where to look for `acvm.exe` and the standard library when `--runtime` was not given.
///
/// Four answers, most specific first:
///
///   1. `$ACVM_HOME` -- an explicit choice for this shell, which is what a user with two
///      toolchains installed needs.
///   2. this executable's own directory, but only when it actually holds a runtime. A
///      packer copied next to a toolchain should use that one.
///   3. the build directory this packer was compiled against, baked in at configure time.
///      Same idea as the compiler's own standard-library path: a tool that cannot find
///      its siblings is a tool every caller has to configure.
///
/// The check for "is this really a runtime" is the same in each case, so a wrong guess
/// falls through to the next instead of failing later with a confusing message.
bool holdsRuntime(const fs::path& dir) {
    if (dir.empty()) {
        return false;
    }
    std::error_code error;
    return fs::exists(dir / "acvm.exe", error) || fs::exists(dir / "acvm", error);
}

fs::path defaultRuntimeDir() {
    if (const char* home = std::getenv("ACVM_HOME"); home != nullptr && home[0] != '\0') {
        if (holdsRuntime(fs::path(home))) {
            return fs::path(home);
        }
    }
    const fs::path beside = executableDir();
    if (holdsRuntime(beside)) {
        return beside;
    }
#ifdef ACVM_RUNTIME_DIR
    return fs::path(ACVM_RUNTIME_DIR);
#else
    return beside;
#endif
}

int usageError(const std::string& message) {
    std::cerr << "apack: " << message << "\n\n";
    printUsage(std::cerr);
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
        printUsage(std::cout);
        return 0;
    }
    for (const std::string& arg : args) {
        if (arg == "help" || arg == "--help" || arg == "-h") {
            printUsage(std::cout);
            return 0;
        }
        if (arg == "version" || arg == "--version") {
            std::cout << "apack " << kVersion << '\n';
            return 0;
        }
    }

    acvm::PackRequest request;
    std::string output;
    std::vector<std::string> positional;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "-o") {
            if (i + 1 >= args.size()) {
                return usageError("'-o' requires a directory");
            }
            output = args[++i];
        } else if (arg == "--name") {
            if (i + 1 >= args.size()) {
                return usageError("'--name' requires a name");
            }
            request.appName = args[++i];
        } else if (arg == "--runtime") {
            if (i + 1 >= args.size()) {
                return usageError("'--runtime' requires a directory");
            }
            request.runtimeDir = args[++i];
        } else if (arg.size() == 3 && arg[0] == '-' && arg[1] == 'O' && arg[2] >= '0' &&
                   arg[2] <= '4') {
            request.optimize = arg[2] - '0';
        } else if (arg == "--single") {
            request.singleFile = true;
        } else if (arg == "--app-version" || arg == "--company" || arg == "--copyright" ||
                   arg == "--icon") {
            if (i + 1 >= args.size()) {
                return usageError("'" + arg + "' requires a value");
            }
            const std::string value = args[++i];
            if (arg == "--app-version") {
                request.version = value;
            } else if (arg == "--company") {
                request.company = value;
            } else if (arg == "--copyright") {
                request.copyright = value;
            } else {
                request.icon = value;
            }
        } else if (arg.size() > 1 && arg[0] == '-') {
            return usageError("unknown option '" + arg + "'");
        } else {
            positional.push_back(arg);
        }
    }

    if (positional.empty()) {
        return usageError("no input file given");
    }
    if (positional.size() > 1) {
        return usageError("unexpected argument '" + positional[1] + "'");
    }
    if (output.empty()) {
        return usageError("'-o' is required: a bundle needs somewhere to go");
    }

    request.source = positional[0];
    request.outputDir = output;
    if (request.runtimeDir.empty()) {
        request.runtimeDir = defaultRuntimeDir().string();
    }

    return acvm::pack(request, std::cout, std::cerr);
}
