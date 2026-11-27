//===--- Packer.cpp - Assembles a runnable bundle from a .ax program ------===//
#include "Packer.h"

#include "acvm/Driver/Driver.h"
#include "acvm/Driver/Payload.h"
#include "acvm/IR/Serializer.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace acvm {
namespace {

namespace fs = std::filesystem;

/// The shared object name for a `library "x"` declaration.
///
/// Both spellings are tried because the two platforms do not agree: Windows drops the
/// prefix and uses `.dll`, everything else prefixes `lib` and uses `.so`.
std::vector<std::string> libraryFileNames(const std::string& library) {
#if defined(_WIN32)
    return {library + ".dll"};
#else
    return {"lib" + library + ".so", library + ".so"};
#endif
}

/// Finds the shared object for a dependency, or an empty path.
///
/// Two places, in the order that makes a project-local library win over a standard one:
///
///   1. next to the `.axd` that declared it. This is what makes a module like `gui`, which
///      lives in the project rather than in the standard library, pack correctly.
///   2. next to the runtime. The standard library ships beside the compiler, not beside
///      its own sources, so this is where `io.dll` and friends are.
///
/// Nothing else is searched. A `PATH` lookup would make the result depend on the machine
/// the packer runs on, and a bundle whose contents vary by machine is not a bundle.
fs::path findLibrary(const ModuleDependency& dependency, const fs::path& runtimeDir) {
    const std::vector<std::string> names = libraryFileNames(dependency.library);
    if (!dependency.path.empty()) {
        const fs::path beside = fs::path(dependency.path).parent_path();
        for (const std::string& name : names) {
            std::error_code error;
            const fs::path candidate = beside / name;
            if (fs::exists(candidate, error)) {
                return candidate;
            }
        }
    }
    for (const std::string& name : names) {
        std::error_code error;
        const fs::path candidate = runtimeDir / name;
        if (fs::exists(candidate, error)) {
            return candidate;
        }
    }
    return {};
}

/// The libraries the statically linked runtime carries.
///
/// Mirrors `ACVM_STDLIB_MODULES` and `ACVM_EXTRALIB_MODULES` in the Axiom build. Duplicated
/// because this project is configured *against* a built tree rather than within it, and
/// CMake has no way to hand a list across that boundary.
///
/// A duplicated list is a list that can go stale, which is why it is used to *refuse* and
/// not to decide: if it names a library the runtime does not actually carry, the pack fails
/// here with a sentence. The alternative -- trusting it and discovering the difference on
/// the recipient's machine -- is the failure this whole tool exists to prevent.
bool isStaticallyLinked(const std::string& library) {
    static const std::vector<std::string> kLinked = {
        "stdlib",  "algorithm", "random", "io",   "time",
        "os",      "parallel",  "threadpool", "adb", "net",
    };
    return std::find(kLinked.begin(), kLinked.end(), library) != kLinked.end();
}

#if defined(_WIN32)
constexpr const char* kExecutableName = "acvm.exe";
/// The runtime with the standard library linked in, used for single-file bundles. It is a
/// separate build because the ordinary runtime deliberately does not carry ten modules it
/// may never call.
constexpr const char* kStaticExecutableName = "acvm-static.exe";
#else
constexpr const char* kExecutableName = "acvm";
constexpr const char* kStaticExecutableName = "acvm-static";
#endif

/// True when the shared object can be loaded with its own directory as the search root.
///
/// This is the check that catches the failure a bundle actually suffers: a library that was
/// not shipped, or one that is present but cannot find a dependency of its own. It does not
/// run any of the program's code -- loading a library runs its static initialisers, which
/// is why the search path is pinned to the bundle instead of the ambient one.
bool loadsFrom(const fs::path& library) {
#if defined(_WIN32)
    HMODULE handle = LoadLibraryExW(library.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (handle == nullptr) {
        return false;
    }
    FreeLibrary(handle);
    return true;
#else
    void* handle = dlopen(library.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        return false;
    }
    dlclose(handle);
    return true;
#endif
}

std::string stemOf(const std::string& path) {
    const std::string name = fs::path(path).filename().string();
    const std::size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

} // namespace

int pack(const PackRequest& request, std::ostream& out, std::ostream& err) {
    const fs::path source = fs::path(request.source).lexically_normal();
    std::error_code error;
    if (!fs::exists(source, error)) {
        err << "apack: no such file: " << request.source << "\n";
        return 2;
    }

    const std::string appName = request.appName.empty() ? stemOf(request.source) : request.appName;
    if (appName.empty()) {
        err << "apack: cannot derive an application name from '" << request.source << "'\n";
        return 2;
    }

    const fs::path outputDir = fs::path(request.outputDir).lexically_normal();
    const fs::path runtimeDir =
        request.runtimeDir.empty() ? fs::path(".") : fs::path(request.runtimeDir).lexically_normal();

    // Copying the runtime into the directory it lives in would truncate the running
    // executable on some systems and is never what was meant.
    if (fs::equivalent(outputDir, runtimeDir, error)) {
        err << "apack: the output directory is the runtime directory; pick another\n";
        return 2;
    }

    const fs::path runtime =
        runtimeDir / (request.singleFile ? kStaticExecutableName : kExecutableName);
    if (!fs::exists(runtime, error)) {
        err << "apack: cannot find the runtime: " << runtime.generic_string() << "\n";
        if (request.singleFile) {
            err << "           --single needs the statically linked runtime, which carries\n"
                << "           the standard library; build it with\n"
                << "           cmake --build <Axiom build> --target acvm_static\n";
        }
        err << "           pass --runtime <dir> to point at the toolchain\n";
        return 2;
    }

    fs::create_directories(outputDir, error);
    if (error) {
        err << "apack: cannot create " << outputDir.generic_string() << ": " << error.message()
            << "\n";
        return 2;
    }

    // ── compile ─────────────────────────────────────────────────────────────
    //
    // The bytecode goes straight into the bundle: the source is not shipped, because it is
    // not needed at run time and a bundle that carries its own source invites someone to
    // edit the copy that is never read.
    const fs::path payload =
        outputDir / (request.singleFile ? (appName + ".acvm.tmp") : (appName + ".acvm"));
    DriverOptions options;
    options.optimize = static_cast<OptLevel>(std::clamp(request.optimize, 0, 4));
    Driver driver(out, err);
    const int status = driver.buildSource(source.string(), payload.string(), options);
    if (status != 0) {
        err << "apack: compile failed; nothing was written\n";
        // A half-written bundle is worse than none: it looks finished.
        fs::remove(payload, error);
        return status;
    }

    // ── collect ─────────────────────────────────────────────────────────────
    //
    // The dependency list comes from the compile that just ran, not from a scan of the
    // sources. Recomputing it would be a second implementation of module resolution, and
    // the two would disagree the first time a search rule changed.
    std::vector<BundleEntry> entries;
    entries.push_back({runtime.string(), (outputDir / (appName + ".exe")).string()});

    std::vector<std::string> libraries;
    std::vector<const ModuleDependency*> sources;
    for (const ModuleDependency& dependency : driver.dependencies()) {
        if (dependency.library.empty()) {
            continue;
        }
        if (std::find(libraries.begin(), libraries.end(), dependency.library) != libraries.end()) {
            continue;
        }
        libraries.push_back(dependency.library);
        sources.push_back(&dependency);
    }

    std::vector<std::string> missing;
    if (request.singleFile) {
        // Nothing is copied beside the executable, so the only question is whether the
        // statically linked runtime can serve every module the program reaches. A module
        // outside the standard library -- a project's own `gui`, for instance -- has no
        // static counterpart, and shipping the executable without it would fail on the
        // recipient's machine at the first call.
        for (const std::string& library : libraries) {
            if (!isStaticallyLinked(library)) {
                missing.push_back(library);
            }
        }
        if (!missing.empty()) {
            err << "apack: --single cannot bundle these, because they are not part of the\n"
                << "       statically linked runtime:\n";
            for (const std::string& name : missing) {
                err << "           " << name << "\n";
            }
            err << "       use a directory bundle, which ships the library beside the "
                   "executable\n";
            fs::remove(payload, error);
            return 3;
        }
    } else {
        for (const ModuleDependency* dependency : sources) {
            const fs::path library = findLibrary(*dependency, runtimeDir);
            if (library.empty()) {
                missing.push_back(dependency->library);
                continue;
            }
            entries.push_back({library.string(), (outputDir / library.filename()).string()});
        }

        if (!missing.empty()) {
            // Failing here rather than shipping is the whole point of the tool. A bundle
            // missing `parallel.dll` looks fine and traps on the first call to it, which
            // is after it has been sent to someone.
            err << "apack: these libraries are required but were not found:\n";
            for (const std::string& name : missing) {
                err << "           " << name << "\n";
            }
            err << "           looked next to each module and next to the runtime ("
                << runtimeDir.generic_string() << ")\n";
            fs::remove(payload, error);
            return 3;
        }
    }

    // ── copy ────────────────────────────────────────────────────────────────
    for (const BundleEntry& entry : entries) {
        const fs::path to = fs::path(entry.to);
        if (fs::equivalent(fs::path(entry.from), to, error)) {
            continue;   // already in place
        }
        fs::copy_file(entry.from, to, fs::copy_options::overwrite_existing, error);
        if (error) {
            err << "apack: cannot copy " << entry.from << " -> " << to.generic_string() << ": "
                << error.message() << "\n";
            return 4;
        }
    }

    // ── assemble ────────────────────────────────────────────────────────────
    //
    // Single file: the payload is appended to the runtime, and the intermediate bytecode is
    // removed, so what remains in the output directory is one executable and nothing else.
    if (request.singleFile) {
        const fs::path launcher = outputDir / (appName + ".exe");
        std::string appendError;
        if (!appendPayload(launcher.string(), payload.string(), appendError)) {
            err << "apack: " << appendError << "\n";
            fs::remove(payload, error);
            return 4;
        }
        fs::remove(payload, error);
    }

    // ── check ───────────────────────────────────────────────────────────────
    //
    // Two checks, neither of which executes the program. A GUI program would open a window
    // and a program that waits for input would hang, so "run it once" is not available
    // here; what is available covers the failures that actually reach a user.
    //
    //   1. the payload parses. This catches a truncated or mismatched bytecode file, and in
    //      single-file mode it also proves the append wrote what the footer claims -- the
    //      bytes are read back out of the executable rather than off the intermediate file.
    //   2. every shipped library loads from the bundle. This catches the missing or
    //      unloadable shared object, which is the failure that reaches a user as
    //      "it doesn't start on my machine".
    std::string bytes;
    if (request.singleFile) {
        const fs::path launcher = outputDir / (appName + ".exe");
        std::string readError;
        if (!readEmbeddedPayload(launcher.string(), bytes, readError)) {
            err << "apack: " << (readError.empty() ? "no payload was written" : readError) << "\n";
            return 5;
        }
    } else {
        std::ifstream in(payload, std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    Module module;
    std::string moduleError;
    if (!loadModuleFromMemory(module, bytes.data(), bytes.size(), appName, moduleError) ||
        module.entryPoint < 0) {
        err << "apack: the payload does not read back; the bundle is not usable\n";
        return 5;
    }

    if (!request.singleFile) {
        for (const BundleEntry& entry : entries) {
            const fs::path copied = fs::path(entry.to);
            if (copied.extension() == ".acvm" || copied.extension() == ".exe") {
                continue;
            }
            if (!loadsFrom(copied)) {
                err << "apack: " << copied.filename().generic_string()
                    << " does not load from the bundle\n";
                return 5;
            }
        }
    }

    out << "packed " << source.filename().generic_string() << " -> " << outputDir.generic_string()
        << "/\n";
    if (request.singleFile) {
        const fs::path launcher = outputDir / (appName + ".exe");
        out << "  " << appName << ".exe   " << fs::file_size(launcher, error)
            << " bytes   runtime and program, self-contained\n";
        return 0;
    }
    out << "  " << appName << ".exe      runtime\n";
    out << "  " << appName << ".acvm     " << bytes.size() << " bytes\n";
    for (const BundleEntry& entry : entries) {
        const fs::path to = fs::path(entry.to);
        if (to.filename() == (appName + ".exe")) {
            continue;
        }
        out << "  " << to.filename().generic_string() << "\n";
    }
    return 0;
}

} // namespace acvm
