//===--- Resources.h - Identity stamped into the produced executable -----===//
#pragma once

#include <string>

namespace apack {

/// What the produced executable says about itself.
///
/// This is the difference between a file that runs and a file someone will install. An
/// executable with no version resource shows a blank Details tab, no publisher, and no
/// version to compare; one with no icon shows the generic application glyph in Explorer.
struct AppInfo {
    /// Shown as the file description and as the product name.
    std::string name;
    /// `major.minor.patch.build`. Missing components are filled with zero.
    std::string version = "1.0.0.0";
    std::string company;
    std::string copyright;
    /// Path to an `.ico`. Empty leaves the runtime's own icon in place.
    std::string icon;
    /// Written to `OriginalFilename`, which is what the Details tab shows under that name.
    std::string originalFilename;
};

/// Rewrites the identity resources of `executablePath` in place.
///
/// Uses `BeginUpdateResource` rather than a resource compiler: `llvm-rc` produces a `.res`,
/// and folding a `.res` into an existing image means relinking it -- which would require a
/// linker on the machine doing the packing, and that machine is the user's. The update API
/// edits the image directly, so the packer stays a single self-contained binary.
///
/// A version resource and a manifest are always written. The icon is written only when one
/// was given.
bool applyResources(const std::string& executablePath, const AppInfo& info, std::string& error);

} // namespace apack
