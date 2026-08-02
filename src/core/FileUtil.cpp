#include "dkrport/core/FileUtil.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace dkrport {

bool ReadBinaryFile(const std::filesystem::path& path, std::vector<std::uint8_t>& data, std::string& error,
                    std::uint64_t maxBytes) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        error = "Could not determine file size: " + ec.message();
        return false;
    }
    if (size > maxBytes) {
        error = "File is larger than the configured safety limit.";
        return false;
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = "Could not open the file for reading.";
        return false;
    }

    data.resize(static_cast<std::size_t>(size));
    if (!data.empty()) {
        stream.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    if (!stream && !data.empty()) {
        error = "The file could not be read completely.";
        data.clear();
        return false;
    }
    return true;
}

bool ReadTextFile(const std::filesystem::path& path, std::string& text, std::string& error,
                  std::uint64_t maxBytes) {
    std::vector<std::uint8_t> data;
    if (!ReadBinaryFile(path, data, error, maxBytes)) return false;
    text.assign(data.begin(), data.end());
    return true;
}

bool WriteBinaryFileAtomic(const std::filesystem::path& path, const std::vector<std::uint8_t>& data,
                           std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        error = "Could not create output directory: " + ec.message();
        return false;
    }

    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) {
            error = "Could not create temporary output file.";
            return false;
        }
        if (!data.empty()) {
            stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        }
        stream.flush();
        if (!stream) {
            error = "Could not finish writing temporary output file.";
            return false;
        }
    }

#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD code = GetLastError();
        std::filesystem::remove(temporary, ec);
        error = "Could not atomically replace output file (Windows error " + std::to_string(code) + ").";
        return false;
    }
#else
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::filesystem::remove(temporary);
        error = "Could not atomically replace output file: " + ec.message();
        return false;
    }
#endif
    return true;
}

bool WriteTextFileAtomic(const std::filesystem::path& path, const std::string& text, std::string& error) {
    return WriteBinaryFileAtomic(path, std::vector<std::uint8_t>(text.begin(), text.end()), error);
}

std::string JsonEscape(const std::string& input) {
    std::ostringstream output;
    for (const char rawCharacter : input) {
        const unsigned char character = static_cast<unsigned char>(rawCharacter);
        switch (character) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (character < 0x20U) {
                    output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                           << static_cast<unsigned int>(character) << std::dec;
                } else {
                    output << static_cast<char>(character);
                }
                break;
        }
    }
    return output.str();
}

std::filesystem::path PathFromUtf8(const std::string& input) {
    std::u8string utf8;
    utf8.reserve(input.size());
    for (const char character : input) utf8.push_back(static_cast<char8_t>(static_cast<unsigned char>(character)));
    return std::filesystem::path(utf8);
}

std::string PathToUtf8(const std::filesystem::path& path) {
    const std::u8string utf8 = path.u8string();
    std::string output;
    output.reserve(utf8.size());
    for (const char8_t character : utf8) output.push_back(static_cast<char>(character));
    return output;
}

std::string LowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

} // namespace dkrport
