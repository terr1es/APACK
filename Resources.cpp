//===--- Resources.cpp - Identity stamped into the produced executable ----===//
#include "Resources.h"

#if defined(_WIN32)
#include <windows.h>
#endif

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace apack {
namespace {

#if defined(_WIN32)

using Bytes = std::vector<unsigned char>;

void putWord(Bytes& out, std::uint16_t value) {
    out.push_back(static_cast<unsigned char>(value & 0xFF));
    out.push_back(static_cast<unsigned char>((value >> 8) & 0xFF));
}

void putDword(Bytes& out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<unsigned char>((value >> (8 * i)) & 0xFF));
    }
}

void append(Bytes& to, const Bytes& from) {
    to.insert(to.end(), from.begin(), from.end());
}

void putWide(Bytes& out, const std::wstring& text) {
    for (wchar_t character : text) {
        putWord(out, static_cast<std::uint16_t>(character));
    }
    putWord(out, 0);   // every string block is null-terminated
}

/// Resource blocks are DWORD-aligned, including the space between a key and its value.
void padToDword(Bytes& out) {
    while (out.size() % 4 != 0) {
        out.push_back(0);
    }
}

std::wstring widen(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (length <= 1) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(length - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wide.data(), length);
    return wide;
}

/// One node of a `VS_VERSIONINFO` tree.
///
/// The layout is a length-prefixed header, a null-terminated key, a value, and then
/// children -- each part padded to a DWORD. It is built bottom-up because the length field
/// is the total size of the node, which is only known once the children exist.
Bytes makeBlock(const std::wstring& key, std::uint16_t valueLength, std::uint16_t type,
                const Bytes& value, const Bytes& children) {
    Bytes block;
    putWord(block, 0);            // total length, patched below
    putWord(block, valueLength);
    putWord(block, type);
    putWide(block, key);
    padToDword(block);
    block.insert(block.end(), value.begin(), value.end());
    if (!value.empty()) {
        padToDword(block);
    }
    block.insert(block.end(), children.begin(), children.end());

    const auto length = static_cast<std::uint16_t>(block.size());
    block[0] = static_cast<unsigned char>(length & 0xFF);
    block[1] = static_cast<unsigned char>((length >> 8) & 0xFF);
    return block;
}

Bytes makeString(const std::wstring& key, const std::wstring& value) {
    Bytes text;
    putWide(text, value);
    // For a text block the value length counts characters, terminator included.
    return makeBlock(key, static_cast<std::uint16_t>(value.size() + 1), 1, text, {});
}

struct Version {
    std::uint16_t major = 1;
    std::uint16_t minor = 0;
    std::uint16_t patch = 0;
    std::uint16_t build = 0;
};

/// `1`, `1.2`, `1.2.3` and `1.2.3.4` are all accepted; missing parts are zero.
///
/// Rejected input is not an error worth failing a pack over -- a malformed version string
/// is a typo, and the version resource is metadata. The unparseable parts become zero.
Version parseVersion(const std::string& text) {
    Version version;
    std::uint16_t* slots[4] = {&version.major, &version.minor, &version.patch, &version.build};
    std::size_t slot = 0;
    std::size_t start = 0;
    while (slot < 4 && start <= text.size()) {
        const std::size_t dot = text.find('.', start);
        const std::string part =
            text.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        if (!part.empty()) {
            *slots[slot] = static_cast<std::uint16_t>(std::strtoul(part.c_str(), nullptr, 10));
        }
        ++slot;
        if (dot == std::string::npos) {
            break;
        }
        start = dot + 1;
    }
    return version;
}

std::string versionText(const Version& version) {
    return std::to_string(version.major) + "." + std::to_string(version.minor) + "." +
           std::to_string(version.patch) + "." + std::to_string(version.build);
}

/// The `VS_FIXEDFILEINFO` block: thirteen DWORDs describing the file, in the fixed order
/// Windows expects. The signature is what makes the resource recognisable at all.
Bytes makeFixedFileInfo(const Version& version) {
    const std::uint32_t ms = (static_cast<std::uint32_t>(version.major) << 16) | version.minor;
    const std::uint32_t ls = (static_cast<std::uint32_t>(version.patch) << 16) | version.build;

    Bytes info;
    putDword(info, 0xFEEF04BD);   // dwSignature
    putDword(info, 0x00010000);   // dwStrucVersion
    putDword(info, ms);           // dwFileVersionMS
    putDword(info, ls);           // dwFileVersionLS
    putDword(info, ms);           // dwProductVersionMS
    putDword(info, ls);           // dwProductVersionLS
    putDword(info, 0x3F);         // dwFileFlagsMask
    putDword(info, 0);            // dwFileFlags
    putDword(info, 0x00040004);   // dwFileOS: VOS_NT_WINDOWS32
    putDword(info, 1);            // dwFileType: VFT_APP
    putDword(info, 0);            // dwFileSubtype
    putDword(info, 0);            // dwFileDateMS
    putDword(info, 0);            // dwFileDateLS
    return info;
}

/// Language 0x0409, code page 0x04B0 -- US English, Unicode. The table key is that pair
/// written as hex, and it has to match the translation advertised in `VarFileInfo` or
/// Explorer shows a version resource with no fields in it.
constexpr WORD kLanguage = 0x0409;
constexpr WORD kCodePage = 0x04B0;

Bytes makeVersionInfo(const AppInfo& info, const std::string& originalFilename) {
    const Version version = parseVersion(info.version);
    const std::wstring text = widen(versionText(version));

    Bytes table;
    append(table, makeString(L"CompanyName", widen(info.company)));
    append(table, makeString(L"FileDescription", widen(info.name)));
    append(table, makeString(L"FileVersion", text));
    append(table, makeString(L"InternalName", widen(info.name)));
    append(table, makeString(L"LegalCopyright", widen(info.copyright)));
    append(table, makeString(L"OriginalFilename", widen(originalFilename)));
    append(table, makeString(L"ProductName", widen(info.name)));
    append(table, makeString(L"ProductVersion", text));

    Bytes stringTable = makeBlock(L"040904B0", 0, 1, {}, table);
    Bytes stringFileInfo = makeBlock(L"StringFileInfo", 0, 1, {}, stringTable);

    Bytes translation;
    putWord(translation, kLanguage);
    putWord(translation, kCodePage);
    Bytes variable = makeBlock(L"Translation", 4, 0, translation, {});
    Bytes varFileInfo = makeBlock(L"VarFileInfo", 0, 1, {}, variable);

    Bytes children = stringFileInfo;
    append(children, varFileInfo);

    return makeBlock(L"VS_VERSION_INFO", 52, 0, makeFixedFileInfo(version), children);
}

/// A manifest that asks for the modern common controls, states that the program does not
/// need elevation, and declares DPI awareness.
///
/// The first item is not cosmetic: without it the common controls fall back to the Windows
/// 95 drawing code, so every button, list and group box in a GUI program renders in the
/// old style. The third matters on any display scaled above 100%, where an unaware process
/// is rendered at 96 DPI and stretched.
const char* kManifest =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
    "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">\n"
    "  <dependency>\n"
    "    <dependentAssembly>\n"
    "      <assemblyIdentity type=\"win32\" name=\"Microsoft.Windows.Common-Controls\"\n"
    "        version=\"6.0.0.0\" processorArchitecture=\"*\"\n"
    "        publicKeyToken=\"6595b64144ccf1df\" language=\"*\"/>\n"
    "    </dependentAssembly>\n"
    "  </dependency>\n"
    "  <trustInfo xmlns=\"urn:schemas-microsoft-com:asm.v3\">\n"
    "    <security>\n"
    "      <requestedPrivileges>\n"
    "        <requestedExecutionLevel level=\"asInvoker\" uiAccess=\"false\"/>\n"
    "      </requestedPrivileges>\n"
    "    </security>\n"
    "  </trustInfo>\n"
    "  <application xmlns=\"urn:schemas-microsoft-com:asm.v3\">\n"
    "    <windowsSettings>\n"
    "      <dpiAware xmlns=\"http://schemas.microsoft.com/SMI/2005/WindowsSettings\">true/pm"
    "</dpiAware>\n"
    "      <dpiAwareness xmlns=\"http://schemas.microsoft.com/SMI/2016/WindowsSettings\">"
    "permonitorv2</dpiAwareness>\n"
    "    </windowsSettings>\n"
    "  </application>\n"
    "</assembly>\n";

bool readFileBytes(const std::string& path, Bytes& bytes, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open '" + path + "'";
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

std::uint32_t readDword(const Bytes& bytes, std::size_t at) {
    return static_cast<std::uint32_t>(bytes[at]) | (static_cast<std::uint32_t>(bytes[at + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[at + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
}

std::uint16_t readWord(const Bytes& bytes, std::size_t at) {
    return static_cast<std::uint16_t>(bytes[at] | (bytes[at + 1] << 8));
}

/// Adds every image in an `.ico` as an icon resource, plus the group that names them.
///
/// The shell picks the icon from the *group*, not from the individual images, so a file
/// with images but no group shows the generic glyph -- which is the failure this function
/// exists to avoid. Group id 1 is the one Explorer reads.
bool addIcon(HANDLE update, const std::string& path, std::string& error) {
    Bytes file;
    if (!readFileBytes(path, file, error)) {
        return false;
    }
    if (file.size() < 6 || readWord(file, 0) != 0 || readWord(file, 2) != 1) {
        error = "'" + path + "' is not a Windows icon file";
        return false;
    }
    const std::uint16_t count = readWord(file, 4);
    if (count == 0 || file.size() < 6 + static_cast<std::size_t>(count) * 16) {
        error = "'" + path + "' has no icon images";
        return false;
    }

    Bytes group;
    putWord(group, 0);        // reserved
    putWord(group, 1);        // type: icon
    putWord(group, count);

    for (std::uint16_t i = 0; i < count; ++i) {
        const std::size_t entry = 6 + static_cast<std::size_t>(i) * 16;
        const std::uint32_t size = readDword(file, entry + 8);
        const std::uint32_t offset = readDword(file, entry + 12);
        if (offset > file.size() || size > file.size() - offset) {
            error = "'" + path + "' has an image outside the file";
            return false;
        }

        const WORD id = static_cast<WORD>(i + 1);
        if (!UpdateResourceW(update, RT_ICON, MAKEINTRESOURCEW(id), kLanguage,
                             file.data() + offset, size)) {
            error = "cannot write icon image " + std::to_string(id);
            return false;
        }

        // The group entry is the directory entry with the offset replaced by the id.
        for (int b = 0; b < 8; ++b) {
            group.push_back(file[entry + b]);   // width, height, colours, reserved, planes, bits
        }
        putDword(group, size);
        putWord(group, id);
    }

    if (!UpdateResourceW(update, RT_GROUP_ICON, MAKEINTRESOURCEW(1), kLanguage, group.data(),
                         static_cast<DWORD>(group.size()))) {
        error = "cannot write the icon group";
        return false;
    }
    return true;
}

#endif // _WIN32

} // namespace

bool applyResources(const std::string& executablePath, const AppInfo& info, std::string& error) {
#if !defined(_WIN32)
    // The identity resources described here are a PE concept. On other platforms the
    // executable is left as it is rather than failing a pack over metadata.
    (void)executablePath;
    (void)info;
    (void)error;
    return true;
#else
    std::wstring widePath = widen(executablePath);
    if (widePath.empty()) {
        error = "cannot address '" + executablePath + "'";
        return false;
    }

    HANDLE update = BeginUpdateResourceW(widePath.c_str(), FALSE);
    if (update == nullptr) {
        error = "cannot open '" + executablePath + "' for a resource update";
        return false;
    }

    // Every write happens before `EndUpdateResource`, which commits them all or none: a
    // half-stamped executable that still runs is worse than one that was not touched.
    bool ok = true;

    Bytes version = makeVersionInfo(info, info.originalFilename);
    if (!UpdateResourceW(update, RT_VERSION, MAKEINTRESOURCEW(1), kLanguage, version.data(),
                         static_cast<DWORD>(version.size()))) {
        error = "cannot write the version resource";
        ok = false;
    }

    if (ok) {
        const auto length = static_cast<DWORD>(std::strlen(kManifest));
        if (!UpdateResourceW(update, RT_MANIFEST, MAKEINTRESOURCEW(1), 0,
                             const_cast<char*>(kManifest), length)) {
            error = "cannot write the application manifest";
            ok = false;
        }
    }

    if (ok && !info.icon.empty()) {
        ok = addIcon(update, info.icon, error);
    }

    if (!EndUpdateResourceW(update, ok ? FALSE : TRUE)) {
        if (ok) {
            error = "cannot save the resource update";
        }
        return false;
    }
    return ok;
#endif
}

} // namespace apack
