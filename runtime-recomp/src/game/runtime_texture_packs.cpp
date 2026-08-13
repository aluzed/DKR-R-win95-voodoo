#include "runtime_texture_packs.hpp"

#include "rice_texture_pack_policy.hpp"
#include "runtime_rice_texture_import.hpp"

#include "common/rt64_filesystem.h"
#include "common/rt64_filesystem_zip.h"
#include "hle/rt64_application.h"
#include "render/rt64_texture_cache.h"

#include <json/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <set>
#include <system_error>
#include "win95/fileio.hpp"
#include "win95/sync.hpp"

namespace {

dkr::sync::mutex g_mutex;
std::filesystem::path g_pack_directory;
std::filesystem::path g_settings_path;
std::vector<dkr::runtime::texture_packs::PackInfo> g_packs;
std::set<std::string> g_enabled_ids;
std::set<std::string> g_hidden_ids;
std::set<std::string> g_applied_ids;
std::string g_status;
std::uint64_t g_generation = 1;
std::uint64_t g_applied_generation = 0;
bool g_applied_modern = false;
std::vector<RT64::ReplacementDirectory> g_applied_replacements;

struct PendingDeletion {
    std::string id;
    std::filesystem::path path;
    std::string name;
};

std::vector<PendingDeletion> g_pending_deletions;

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return value;
}

std::string Utf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {value.begin(), value.end()};
}

std::string StableId(const std::filesystem::path& path) {
    return Lower(path.filename().string());
}

std::string DisplayName(const std::filesystem::path& path) {
    std::string result = path.stem().string();
    std::replace(result.begin(), result.end(), '_', ' ');
    return result.empty() ? path.filename().string() : result;
}

bool IsImage(const std::string& entry) {
    const std::string extension = Lower(
        std::filesystem::path(entry).extension().string());
    return extension == ".png" || extension == ".dds" ||
           extension == ".jpg" || extension == ".jpeg";
}

bool LooksLikeRiceName(const std::string& entry) {
    const std::string lower = Lower(entry);
    return lower.find("#0#") != std::string::npos ||
           lower.find("_rgb") != std::string::npos ||
           lower.find("_all") != std::string::npos ||
           lower.find("_ci") != std::string::npos;
}

void LoadSettingsLocked() {
    g_enabled_ids.clear();
    g_hidden_ids.clear();
    std::ifstream input(g_settings_path.string());
    std::string line;
    while (std::getline(input, line)) {
        constexpr const char* enabled_prefix = "enabled=";
        constexpr const char* hidden_prefix = "hidden=";
        if (line.rfind(enabled_prefix, 0) == 0 && line.size() > 8) {
            g_enabled_ids.insert(Lower(line.substr(8)));
        } else if (line.rfind(hidden_prefix, 0) == 0 && line.size() > 7) {
            g_hidden_ids.insert(Lower(line.substr(7)));
        }
    }
    for (const auto& id : g_hidden_ids) g_enabled_ids.erase(id);
}

void SaveSettingsLocked() {
    std::error_code error;
    dkr::fs::create_directories(g_settings_path.parent_path(), error);
    const auto temporary = g_settings_path.string() + ".tmp";
    // DKR-WIN95-ALLOW: `temporary` est deja une std::string — la ligne au-dessus la fabrique par .string() + ".tmp" — et non un path : l'ouverture est donc bien etroite. Le controle lit du texte et ne peut pas le savoir.
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) {
        g_status = "Texture-pack preferences could not be saved.";
        return;
    }
    output << "# DKR-R native texture-pack state\n";
    for (const auto& id : g_enabled_ids) output << "enabled=" << id << '\n';
    for (const auto& id : g_hidden_ids) output << "hidden=" << id << '\n';
    output.close();
    dkr::fs::rename(temporary, g_settings_path, error);
    if (error) {
        dkr::fs::remove(g_settings_path, error);
        error.clear();
        dkr::fs::rename(temporary, g_settings_path, error);
    }
    if (error) g_status = "Texture-pack preferences could not be committed.";
}

dkr::runtime::texture_packs::PackInfo InspectArchive(
    const std::filesystem::path& path) {
    using dkr::runtime::texture_packs::Format;
    using dkr::runtime::texture_packs::PackInfo;

    PackInfo info;
    info.id = StableId(path);
    info.name = DisplayName(path);
    info.path = path;

    std::unique_ptr<RT64::FileSystem> archive =
        RT64::FileSystemZip::create(path, "");
    if (!archive) {
        info.detail = "The archive is unreadable or uses an unsupported ZIP compression method.";
        return info;
    }

    std::string database_entry;
    bool has_jabo_manifest = false;
    bool has_rice_names = false;
    std::size_t png_or_dds_count = 0;
    std::size_t jpg_count = 0;
    for (auto iterator = archive->begin(); iterator != archive->end(); ++iterator) {
        const std::string entry = *iterator;
        const std::string lower = Lower(entry);
        if (std::filesystem::path(lower).filename() == "rt64.json") {
            if (database_entry.empty() || entry.size() < database_entry.size()) {
                database_entry = entry;
            }
        }
        if (std::filesystem::path(lower).filename() == "pack.xml") {
            has_jabo_manifest = true;
        }
        if (LooksLikeRiceName(entry)) has_rice_names = true;
        if (IsImage(entry)) {
            ++info.image_count;
            const std::string extension = Lower(
                std::filesystem::path(entry).extension().string());
            if (extension == ".png" || extension == ".dds") ++png_or_dds_count;
            else ++jpg_count;
        }
    }

    if (!database_entry.empty()) {
        std::vector<std::uint8_t> database;
        if (!archive->load(database_entry, database)) {
            info.detail = "The native RT64 database could not be read.";
            return info;
        }
        try {
            const auto parsed = nlohmann::json::parse(database.begin(), database.end());
            if (!parsed.is_object() || !parsed.contains("configuration") ||
                !parsed.contains("textures")) {
                info.detail = "rt64.json is present but does not contain a native RT64 config.";
                return info;
            }
        } catch (const nlohmann::json::exception& exception) {
            info.detail = std::string("rt64.json is invalid: ") + exception.what();
            return info;
        }
        const auto slash = database_entry.find_last_of('/');
        info.zip_base_path = slash == std::string::npos
            ? std::string{} : database_entry.substr(0, slash);
        info.format = Format::NativeRt64;
        info.compatible = true;
        info.detail = std::to_string(info.image_count) +
            " replacement images; validated native RT64 database.";
        return info;
    }

    if (has_jabo_manifest) {
        info.format = Format::LegacyJabo;
        info.detail = "Unsupported: " + std::to_string(info.image_count) +
            " images use the legacy Jabo pack.xml/hash layout" +
            (jpg_count > 0 ? ", including " + std::to_string(jpg_count) +
                " JPG files" : std::string{}) +
            ". DKR-R cannot enable this format.";
    } else if (has_rice_names) {
        info.format = Format::LegacyRice;
        info.detail = std::to_string(info.image_count) +
            " images. Legacy Rice filename layout detected. Convert it with RT64's texture tools to produce rt64.json.";
    } else {
        info.detail = std::to_string(info.image_count) +
            " images, but no rt64.json replacement database was found.";
    }
    (void)png_or_dds_count;
    return info;
}

std::filesystem::path UniqueDestination(const std::filesystem::path& source) {
    std::filesystem::path destination = g_pack_directory / source.filename();
    const std::string stem = destination.stem().string();
    const std::string extension = destination.extension().string();
    std::error_code error;
    for (int suffix = 2; dkr::fs::exists(destination, error) && suffix < 1000;
         ++suffix) {
        destination = g_pack_directory /
            (stem + "-" + std::to_string(suffix) + extension);
    }
    return destination;
}

std::filesystem::path UniqueManagedDestination(const std::filesystem::path& source) {
    std::filesystem::path destination =
        g_pack_directory / (source.stem().string() + ".rice");
    const std::string stem = destination.stem().string();
    std::error_code error;
    for (int suffix = 2; dkr::fs::exists(destination, error) && suffix < 1000;
         ++suffix) {
        destination = g_pack_directory /
            (stem + "-" + std::to_string(suffix) + ".rice");
    }
    return destination;
}

dkr::runtime::texture_packs::PackInfo InspectDirectory(
    const std::filesystem::path& path) {
    using dkr::runtime::texture_packs::Format;
    using dkr::runtime::texture_packs::PackInfo;

    PackInfo info;
    info.id = StableId(path);
    info.name = DisplayName(path);
    info.path = path;

    std::error_code error;
    const auto database_path = path / "rt64.json";
    const auto report_path = path / "dkr-r-rice-import.json";
    const bool managed_rice = dkr::fs::is_regular_file(report_path, error);
    error.clear();
    if (!dkr::fs::is_regular_file(database_path, error)) {
        info.detail = "Managed pack is missing rt64.json.";
        return info;
    }
    try {
        std::ifstream database_file(database_path.string());
        const auto database = nlohmann::json::parse(database_file);
        if (!database.is_object() || !database.contains("configuration") ||
            !database.contains("textures") || !database["textures"].is_array()) {
            info.detail = "Managed rt64.json does not contain a native RT64 database.";
            return info;
        }
        if (!managed_rice) {
            info.image_count = database["textures"].size();
        } else {
            std::set<std::string> rice_identities;
            std::set<std::string> native_aliases;
            for (const auto& texture : database["textures"]) {
                if (!texture.is_object() || !texture.contains("hashes") ||
                    !texture["hashes"].is_object()) {
                    info.detail = "Managed RT64 database contains an invalid texture entry.";
                    return info;
                }
                const std::string rice = Lower(
                    texture["hashes"].value("rice", std::string{}));
                const std::string alias = Lower(
                    texture["hashes"].value("rt64", std::string{}));
                if (!dkr::runtime::rice_texture::valid_identity(rice) ||
                    alias != dkr::runtime::rice_texture::native_alias_string(rice) ||
                    !rice_identities.insert(rice).second ||
                    !native_aliases.insert(alias).second) {
                    info.detail = "Managed Rice database has an invalid, duplicate, or mismatched identity.";
                    return info;
                }
                const auto image_path = path /
                    ("Diddy Kong Racing#" + rice + "_all.png");
                if (!dkr::fs::is_regular_file(image_path, error)) {
                    info.detail = "Managed Rice database is missing a converted PNG for " + rice + ".";
                    return info;
                }
            }
            info.image_count = rice_identities.size();
        }
    } catch (const std::exception& exception) {
        info.detail = std::string("Managed rt64.json is invalid: ") + exception.what();
        return info;
    }

    if (managed_rice) {
        try {
            std::ifstream report_file(report_path.string());
            const auto report = nlohmann::json::parse(report_file);
            const std::size_t source_images = report.value("sourceImages", 0U);
            const std::size_t identities = report.value("convertedIdentities", 0U);
            const std::size_t pairs = report.value("mergedRgbAlphaPairs", 0U);
            info.name = std::filesystem::path(
                report.value("sourceArchive", path.filename().string())).stem().string();
            info.format = Format::RiceRt64;
            info.compatible = identities > 0 && identities == info.image_count;
            info.detail = std::to_string(identities) + "/" +
                std::to_string(identities) + " Rice identities ready from " +
                std::to_string(source_images) + " source PNGs; " +
                std::to_string(pairs) + " split RGB/alpha pairs reconstructed.";
            return info;
        } catch (const std::exception& exception) {
            info.detail = std::string("Rice import report is invalid: ") + exception.what();
            return info;
        }
    }

    info.format = Format::NativeRt64;
    info.compatible = true;
    info.detail = std::to_string(info.image_count) +
        " replacement identities; validated native RT64 directory.";
    return info;
}

bool IsDirectManagedChild(const std::filesystem::path& path) {
    if (g_pack_directory.empty() || path.empty()) return false;
    std::error_code error;
    const std::filesystem::path root =
        dkr::fs::absolute(g_pack_directory, error).lexically_normal();
    if (error) return false;
    error.clear();
    const std::filesystem::path candidate =
        dkr::fs::absolute(path, error).lexically_normal();
    return !error && candidate != root && candidate.parent_path() == root;
}

bool RemoveManagedPath(const std::filesystem::path& path,
                       std::string& error_text) {
    if (!IsDirectManagedChild(path)) {
        error_text = "DKR-R refused to delete a path outside its managed texture-pack folder.";
        return false;
    }
    /* Ecrit en operations sur le chemin plutot que sur un `file_status`.
       Reproduire `file_status` pour Windows 95 aurait demande un type, ses
       accesseurs et ses categories, la ou deux appels disent la meme chose.

       L'ordre est conserve, et il compte : un lien symbolique casse n'« existe »
       pas, et c'est pour cela que le refus est controle avant la presence. Sur
       Windows 95 la question ne se pose pas — il n'y a pas de liens symboliques
       — mais le code est le meme pour toutes les cibles. */
    std::error_code error;
    const bool present = dkr::fs::exists(path, error);
    if (error && error != std::errc::no_such_file_or_directory) {
        error_text = "The managed texture pack could not be inspected: " +
            error.message();
        return false;
    }
    error.clear();
    if (dkr::fs::is_symlink(path)) {
        error_text = "DKR-R will not recursively delete a symbolic link.";
        return false;
    }
    if (present) {
        dkr::fs::remove_all(path, error);
    }
    if (error) {
        error_text = "The managed texture pack could not be deleted: " +
            error.message();
        return false;
    }
    return true;
}

} // namespace

namespace dkr::runtime::texture_packs {

const char* format_name(Format format) {
    switch (format) {
    case Format::NativeRt64: return "Native RT64";
    case Format::RiceRt64: return "Rice / RT64 Bridge";
    case Format::LegacyRice: return "Legacy Rice";
    case Format::LegacyJabo: return "Legacy Jabo";
    default: return "Unknown";
    }
}

void configure(const std::filesystem::path& config_directory) {
    {
        dkr::sync::scoped_lock lock(g_mutex);
        g_pack_directory = config_directory / "texture-packs";
        g_settings_path = config_directory / "texture-packs.ini";
        g_applied_ids.clear();
        g_pending_deletions.clear();
        g_applied_replacements.clear();
        g_applied_generation = 0;
        g_applied_modern = false;
        std::error_code error;
        dkr::fs::create_directories(g_pack_directory, error);
        LoadSettingsLocked();
    }
    refresh();
}

void refresh() {
    std::filesystem::path directory;
    std::set<std::string> enabled;
    std::set<std::string> hidden;
    {
        dkr::sync::scoped_lock lock(g_mutex);
        directory = g_pack_directory;
        enabled = g_enabled_ids;
        hidden = g_hidden_ids;
    }
    std::vector<PackInfo> scanned;
    std::error_code error;
    if (!directory.empty()) {
        dkr::fs::create_directories(directory, error);
        for (const auto& entry : dkr::fs::list_directory(directory, error)) {
            if (error) break;
            if (dkr::fs::is_directory(entry)) {
                if (Lower(entry.extension().string()) == ".importing") continue;
                auto info = InspectDirectory(entry);
                info.hidden = hidden.contains(info.id);
                info.enabled = !info.hidden && info.compatible && enabled.contains(info.id);
                scanned.emplace_back(std::move(info));
                continue;
            }
            if (!entry.is_regular_file(error)) continue;
            const std::string extension = Lower(entry.path().extension().string());
            if (extension != ".zip" && extension != ".rtz") continue;
            auto info = InspectArchive(entry.path());
            info.hidden = hidden.contains(info.id);
            info.enabled = !info.hidden && info.compatible && enabled.contains(info.id);
            scanned.emplace_back(std::move(info));
        }
    }
    std::sort(scanned.begin(), scanned.end(),
              [](const PackInfo& left, const PackInfo& right) {
                  return Lower(left.name) < Lower(right.name);
              });
    {
        dkr::sync::scoped_lock lock(g_mutex);
        g_packs = std::move(scanned);
        ++g_generation;
        if (error) g_status = "The texture-pack folder could not be scanned: " + error.message();
        else if (g_packs.empty()) g_status = "No texture packs imported yet.";
    }
}

std::vector<PackInfo> snapshot(bool include_hidden) {
    dkr::sync::scoped_lock lock(g_mutex);
    if (include_hidden) return g_packs;
    std::vector<PackInfo> visible;
    visible.reserve(g_packs.size());
    std::copy_if(g_packs.begin(), g_packs.end(), std::back_inserter(visible),
                 [](const PackInfo& pack) { return !pack.hidden; });
    return visible;
}

bool import_archive(const std::filesystem::path& source, std::string& status_text) {
    std::error_code error;
    if (!dkr::fs::is_regular_file(source, error)) {
        status_text = "Choose a readable ZIP or RTZ texture-pack archive.";
        return false;
    }
    const std::string extension = Lower(source.extension().string());
    if (extension != ".zip" && extension != ".rtz") {
        status_text = "Texture packs must be supplied as ZIP or RTZ archives.";
        return false;
    }
    constexpr std::uintmax_t maximum_archive_size =
        static_cast<std::uintmax_t>(4) * 1024U * 1024U * 1024U;
    const auto size = dkr::fs::file_size(source, error);
    if (error || size == 0 || size > maximum_archive_size) {
        status_text = "The texture-pack archive is empty, unreadable, or larger than 4 GB.";
        return false;
    }

    PackInfo inspection = InspectArchive(source);
    if (inspection.detail.find("unreadable") != std::string::npos) {
        status_text = inspection.detail;
        return false;
    }

    if (inspection.format == Format::LegacyRice) {
        std::unique_ptr<RT64::FileSystem> archive =
            RT64::FileSystemZip::create(source, "");
        if (!archive) {
            status_text = "The Rice archive could not be reopened for conversion.";
            return false;
        }

        std::filesystem::path destination;
        {
            dkr::sync::scoped_lock lock(g_mutex);
            dkr::fs::create_directories(g_pack_directory, error);
            destination = UniqueManagedDestination(source);
        }
        const std::filesystem::path temporary =
            destination.parent_path() / (destination.filename().string() + ".importing");
        if (temporary.parent_path() != g_pack_directory ||
            destination.parent_path() != g_pack_directory) {
            status_text = "The managed Rice destination failed its safety check.";
            return false;
        }
        dkr::fs::remove_all(temporary, error);
        error.clear();

        rice_texture::ImportResult conversion;
        std::string conversion_error;
        if (!rice_texture::convert_archive(*archive, temporary,
                                           source.filename().string(),
                                           conversion, conversion_error)) {
            dkr::fs::remove_all(temporary, error);
            status_text = "Rice import failed: " + conversion_error;
            return false;
        }
        dkr::fs::rename(temporary, destination, error);
        if (error) {
            const std::string rename_error = error.message();
            dkr::fs::remove_all(temporary, error);
            status_text = "The converted Rice pack could not be committed: " + rename_error;
            return false;
        }
        refresh();
        const auto imported = InspectDirectory(destination);
        status_text = "Imported " + source.filename().string() + " as " +
            format_name(imported.format) + ". " + imported.detail;
        {
            dkr::sync::scoped_lock lock(g_mutex);
            g_status = status_text;
        }
        return imported.compatible;
    }

    std::filesystem::path destination;
    {
        dkr::sync::scoped_lock lock(g_mutex);
        dkr::fs::create_directories(g_pack_directory, error);
        destination = UniqueDestination(source);
    }
    dkr::fs::copy_file_no_overwrite(source, destination, error);
    if (error) {
        status_text = "The texture pack could not be imported: " + error.message();
        return false;
    }
    refresh();
    const auto imported = InspectArchive(destination);
    status_text = "Imported " + destination.filename().string() + " as " +
        format_name(imported.format) + ". " + imported.detail;
    {
        dkr::sync::scoped_lock lock(g_mutex);
        g_status = status_text;
    }
    return true;
}

void set_enabled(const std::string& id, bool enabled) {
    dkr::sync::scoped_lock lock(g_mutex);
    const std::string normalized = Lower(id);
    const auto match = std::find_if(g_packs.begin(), g_packs.end(),
        [&](const PackInfo& pack) { return pack.id == normalized; });
    if (match == g_packs.end() || !match->compatible || match->hidden) return;
    match->enabled = enabled;
    if (enabled) g_enabled_ids.insert(normalized);
    else g_enabled_ids.erase(normalized);
    ++g_generation;
    SaveSettingsLocked();
    g_status = match->name + (enabled ? " queued for live activation." : " queued for live removal.");
}

bool set_hidden(const std::string& id, bool hidden, std::string& status_text) {
    dkr::sync::scoped_lock lock(g_mutex);
    const std::string normalized = Lower(id);
    const auto match = std::find_if(g_packs.begin(), g_packs.end(),
        [&](const PackInfo& pack) { return pack.id == normalized; });
    if (match == g_packs.end()) {
        status_text = "The selected texture pack is no longer available.";
        return false;
    }
    match->hidden = hidden;
    if (hidden) {
        match->enabled = false;
        g_enabled_ids.erase(normalized);
        g_hidden_ids.insert(normalized);
    } else {
        g_hidden_ids.erase(normalized);
    }
    ++g_generation;
    SaveSettingsLocked();
    g_status = match->name + (hidden
        ? " is hidden. Its managed files remain on disk."
        : " is visible in the texture-pack library again.");
    status_text = g_status;
    return true;
}

bool delete_managed(const std::string& id, std::string& status_text) {
    const std::string normalized = Lower(id);
    PackInfo selected;
    bool defer_until_reload = false;
    {
        dkr::sync::scoped_lock lock(g_mutex);
        const auto match = std::find_if(g_packs.begin(), g_packs.end(),
            [&](const PackInfo& pack) { return pack.id == normalized; });
        if (match == g_packs.end()) {
            status_text = "The selected texture pack is no longer available.";
            return false;
        }
        if (!IsDirectManagedChild(match->path)) {
            status_text = "DKR-R refused to delete a path outside its managed texture-pack folder.";
            return false;
        }
        selected = *match;
        defer_until_reload = g_applied_ids.contains(normalized);
    }

    if (!defer_until_reload) {
        if (!RemoveManagedPath(selected.path, status_text)) return false;
    }

    {
        dkr::sync::scoped_lock lock(g_mutex);
        g_enabled_ids.erase(normalized);
        if (defer_until_reload) {
            // Keep a tombstone in the settings file until RT64 has released
            // the archive/directory. A crash before the next reload therefore
            // leaves the pack hidden rather than silently re-enabling it.
            g_hidden_ids.insert(normalized);
            g_pending_deletions.push_back(
                {normalized, selected.path, selected.name});
        } else {
            g_hidden_ids.erase(normalized);
        }
        g_packs.erase(std::remove_if(g_packs.begin(), g_packs.end(),
            [&](const PackInfo& pack) { return pack.id == normalized; }),
            g_packs.end());
        ++g_generation;
        SaveSettingsLocked();
        g_status = defer_until_reload
            ? selected.name +
                " will be permanently deleted after RT64 releases the active pack."
            : selected.name + " was permanently deleted from DKR-R's managed library.";
        status_text = g_status;
    }
    return true;
}

void request_reload() {
    dkr::sync::scoped_lock lock(g_mutex);
    ++g_generation;
}

void apply_pending(RT64::Application& application, bool modern_profile) {
    std::vector<RT64::ReplacementDirectory> replacements;
    std::vector<RT64::ReplacementDirectory> previous_replacements;
    std::set<std::string> replacement_ids;
    std::vector<PendingDeletion> completed_deletions;
    std::uint64_t generation = 0;
    {
        dkr::sync::scoped_lock lock(g_mutex);
        generation = g_generation;
        if (generation == g_applied_generation && modern_profile == g_applied_modern) return;
        previous_replacements = g_applied_replacements;
        if (modern_profile) {
            for (const auto& pack : g_packs) {
                if (pack.enabled && pack.compatible) {
                    replacements.emplace_back(pack.path, pack.zip_base_path);
                    replacement_ids.insert(pack.id);
                }
            }
        }
    }

    const bool cache_available = application.textureCache != nullptr;
    bool success = cache_available &&
        application.textureCache->loadReplacementDirectories(replacements);
    bool restored = false;
    if (!success && cache_available) {
        restored = application.textureCache->loadReplacementDirectories(
            previous_replacements);
    }
    {
        dkr::sync::scoped_lock lock(g_mutex);
        if (success) {
            g_applied_generation = generation;
            g_applied_modern = modern_profile;
            g_applied_replacements = replacements;
            g_applied_ids = replacement_ids;
            completed_deletions.swap(g_pending_deletions);
            g_status = replacements.empty()
                ? "Native texture replacements are disabled."
                : std::to_string(replacements.size()) +
                    " native texture pack(s) active. Existing textures were reloaded safely.";
        } else {
            // Mark this generation as consumed so a rejected archive cannot
            // trigger an expensive reload on every presented frame.
            g_applied_generation = generation;
            g_applied_modern = modern_profile;
            g_status = restored
                ? "RT64 rejected the replacement set; the previous texture set was restored."
                : "RT64 rejected the replacement set and could not restore it. Disable the affected pack before continuing.";
        }
    }
    if (success && !completed_deletions.empty()) {
        std::vector<std::string> deleted_ids;
        std::string deletion_error;
        for (const auto& pending : completed_deletions) {
            std::string error_text;
            if (RemoveManagedPath(pending.path, error_text)) {
                deleted_ids.push_back(pending.id);
            } else if (deletion_error.empty()) {
                deletion_error = pending.name + ": " + error_text;
            }
        }
        dkr::sync::scoped_lock lock(g_mutex);
        for (const auto& deleted_id : deleted_ids) {
            g_hidden_ids.erase(deleted_id);
        }
        SaveSettingsLocked();
        if (!deletion_error.empty()) {
            g_status = "RT64 released the texture pack, but permanent deletion failed: " +
                deletion_error;
        } else {
            g_status = completed_deletions.size() == 1
                ? completed_deletions.front().name +
                    " was permanently deleted from DKR-R's managed library."
                : std::to_string(completed_deletions.size()) +
                    " texture packs were permanently deleted from DKR-R's managed library.";
        }
    }
}

std::string status() {
    dkr::sync::scoped_lock lock(g_mutex);
    return g_status;
}

} // namespace dkr::runtime::texture_packs
