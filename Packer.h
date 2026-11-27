//===--- Packer.h - Turns a .ax program into a bundle that runs elsewhere ---===//
#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace acvm {

/// What to build and where to put it.
struct PackRequest {
    /// The program to package.
    std::string source;
    /// Directory to create (or reuse) for the bundle.
    std::string outputDir;
    /// Name of the launcher, without an extension. Defaults to the source's stem.
    std::string appName;
    /// Where `acvm.exe` and the standard library's shared objects live. Defaults to the
    /// directory holding the packer, which is how the build tree and an installed
    /// toolchain both lay out.
    std::string runtimeDir;
    /// Optimization level for the compiled program, 0-4. Defaults to 2, the same default
    /// the compiler uses.
    int optimize = 2;
    /// Produce one executable instead of a directory.
    ///
    /// The payload is appended to a statically linked runtime, which already contains the
    /// standard library, so the result has no files beside it. Nothing is extracted at run
    /// time: the program is read out of the executable's own bytes.
    bool singleFile = false;

    /// Identity stamped into the produced executable: the version resource, the icon, and
    /// the manifest. `name` comes from `appName`; these are the rest.
    std::string version = "1.0.0.0";
    std::string company;
    std::string copyright;
    /// Path to an `.ico`. Empty leaves the runtime's own icon.
    std::string icon;
};

/// One file the bundle needs.
struct BundleEntry {
    std::string from;
    std::string to;
};

/// Builds the bundle and checks it. Returns 0 on success.
///
/// The checks are deliberate about what they do *not* do: they never run the packaged
/// program. A GUI program would open a window, and a program that waits for input would
/// hang the packer. What is checked instead is what actually breaks in practice -- a
/// shared library that was not shipped, or a payload that does not parse -- and both are
/// checkable without executing anything the program wrote.
int pack(const PackRequest& request, std::ostream& out, std::ostream& err);

} // namespace acvm
