// Pepswitch - AddonManager implementation.
//
// HTTP transport: cpr (libcurl underneath) — same library already used by
// every other network call in TsVitch. We deliberately do NOT instantiate
// libcurl directly; doing so would duplicate the static-link configuration
// that scripts/build_switch.sh wires into the Switch image.

#include "core/addon_manager.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <borealis/core/logger.hpp>
#include <cpr/cpr.h>

#include "utils/config_helper.hpp"

namespace pepswitch {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

AddonManager& AddonManager::instance() {
    static AddonManager s;
    return s;
}

AddonManager::AddonManager() {
    // Lazy load on first instance() touch — keeps construction order safe.
    load();
}

// ---------------------------------------------------------------------------
// Storage
// ---------------------------------------------------------------------------

std::string AddonManager::getStoragePath() {
#ifdef __SWITCH__
    // The user-facing requirement is /switch/stremionx/addons.json on the SD card.
    // libnx mounts the SD card root at "/" so this path resolves directly.
    return "/switch/stremionx/addons.json";
#else
    return ProgramConfig::instance().getConfigDir() + "/stremionx_addons.json";
#endif
}

bool AddonManager::load() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (loaded_.load()) return true;

    const std::string path = getStoragePath();
    std::error_code   ec;
    if (!fs::exists(path, ec)) {
        loaded_.store(true);
        return false;
    }

    try {
        std::ifstream in(path);
        if (!in.good()) {
            brls::Logger::warning("AddonManager: failed to open {}", path);
            loaded_.store(true);
            return false;
        }
        nlohmann::json j;
        in >> j;

        addons_.clear();
        if (j.contains("addons") && j["addons"].is_array()) {
            for (const auto& item : j["addons"]) {
                AddonEntry e;
                e.url     = item.value("url", "");
                e.name    = item.value("name", "");
                e.version = item.value("version", "");
                e.enabled = item.value("enabled", true);
                if (!e.url.empty()) addons_.push_back(std::move(e));
            }
        }
        loaded_.store(true);
        brls::Logger::info("AddonManager: loaded {} addon(s) from {}", addons_.size(), path);
        return true;
    } catch (const std::exception& e) {
        brls::Logger::error("AddonManager: parse error {}: {}", path, e.what());
        loaded_.store(true);
        return false;
    }
}

bool AddonManager::save() const {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string           path = getStoragePath();

    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);

    nlohmann::json j;
    j["addons"] = nlohmann::json::array();
    for (const auto& e : addons_) {
        j["addons"].push_back({
            {"url", e.url},
            {"name", e.name},
            {"version", e.version},
            {"enabled", e.enabled},
        });
    }

    try {
        std::ofstream out(path, std::ios::trunc);
        if (!out.good()) {
            brls::Logger::error("AddonManager: cannot write {}", path);
            return false;
        }
        out << j.dump(2);
        return true;
    } catch (const std::exception& e) {
        brls::Logger::error("AddonManager: save failed {}: {}", path, e.what());
        return false;
    }
}

// ---------------------------------------------------------------------------
// Add-on list management
// ---------------------------------------------------------------------------

std::vector<AddonEntry> AddonManager::getAddons() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return addons_;
}

bool AddonManager::hasAddon(const std::string& url) const {
    const std::string                 normalized = normalizeBase(url);
    std::lock_guard<std::mutex>       lock(mutex_);
    return std::any_of(addons_.begin(), addons_.end(),
                       [&](const AddonEntry& e) { return e.url == normalized; });
}

bool AddonManager::addAddon(const std::string& url, const std::string& name,
                            const std::string& version) {
    const std::string normalized = normalizeBase(url);
    if (normalized.empty()) return false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find_if(addons_.begin(), addons_.end(),
                               [&](const AddonEntry& e) { return e.url == normalized; });
        if (it != addons_.end()) {
            // Already present — just refresh metadata.
            if (!name.empty()) it->name = name;
            if (!version.empty()) it->version = version;
        } else {
            AddonEntry e;
            e.url     = normalized;
            e.name    = name.empty() ? normalized : name;
            e.version = version;
            e.enabled = true;
            addons_.push_back(std::move(e));
        }
    }
    return save();
}

bool AddonManager::removeAddon(const std::string& url) {
    const std::string normalized = normalizeBase(url);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::remove_if(addons_.begin(), addons_.end(),
                                 [&](const AddonEntry& e) { return e.url == normalized; });
        if (it == addons_.end()) return false;
        addons_.erase(it, addons_.end());
    }
    return save();
}

bool AddonManager::setEnabled(const std::string& url, bool enabled) {
    const std::string normalized = normalizeBase(url);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find_if(addons_.begin(), addons_.end(),
                               [&](const AddonEntry& e) { return e.url == normalized; });
        if (it == addons_.end()) return false;
        it->enabled = enabled;
    }
    return save();
}

// ---------------------------------------------------------------------------
// URL normalization
// ---------------------------------------------------------------------------

std::string AddonManager::normalizeBase(const std::string& raw) {
    std::string url = raw;
    // Trim whitespace.
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    url.erase(url.begin(), std::find_if(url.begin(), url.end(), not_space));
    url.erase(std::find_if(url.rbegin(), url.rend(), not_space).base(), url.end());

    if (url.empty()) return "";

    // Drop a trailing /manifest.json if the user pasted the manifest URL.
    const std::string manifest_suffix = "/manifest.json";
    if (url.size() >= manifest_suffix.size() &&
        url.compare(url.size() - manifest_suffix.size(), manifest_suffix.size(),
                    manifest_suffix) == 0) {
        url.erase(url.size() - manifest_suffix.size());
    }

    // Drop trailing slashes.
    while (!url.empty() && url.back() == '/') url.pop_back();
    return url;
}

// ---------------------------------------------------------------------------
// Manifest fetch
// ---------------------------------------------------------------------------

void AddonManager::fetchManifest(const std::string& url, const ManifestCallback& on_ok,
                                 const ErrorCallback& on_err) {
    const std::string base         = normalizeBase(url);
    const std::string manifest_url = base + "/manifest.json";

    cpr::GetCallback(
        [base, on_ok, on_err](const cpr::Response& r) {
            if (r.error) {
                if (on_err) on_err(r.error.message, -1);
                return;
            }
            if (r.status_code != 200) {
                if (on_err) on_err("HTTP " + std::to_string(r.status_code), r.status_code);
                return;
            }
            try {
                StremioManifest m = parseManifest(base, r.text);
                if (on_ok) on_ok(std::move(m));
            } catch (const std::exception& e) {
                if (on_err) on_err(std::string{"manifest parse: "} + e.what(), -2);
            }
        },
        cpr::Url{manifest_url},
        cpr::HttpVersion{cpr::HttpVersionCode::VERSION_2_0_TLS},
        cpr::Timeout{15000},
        cpr::Header{{"User-Agent", "Pepswitch/0.1 (Stremio)"}});
}

bool AddonManager::fetchManifestSync(const std::string& url, StremioManifest& out,
                                     std::string& err) {
    const std::string base         = normalizeBase(url);
    const std::string manifest_url = base + "/manifest.json";

    cpr::Response r = cpr::Get(cpr::Url{manifest_url}, cpr::Timeout{15000},
                               cpr::Header{{"User-Agent", "Pepswitch/0.1 (Stremio)"}});
    if (r.error) {
        err = r.error.message;
        return false;
    }
    if (r.status_code != 200) {
        err = "HTTP " + std::to_string(r.status_code);
        return false;
    }
    try {
        out = parseManifest(base, r.text);
        return true;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

// ---------------------------------------------------------------------------
// Catalog & stream fetches
// ---------------------------------------------------------------------------

void AddonManager::fetchCatalog(const std::string& addon_url, const std::string& type,
                                const std::string& id, const CatalogCallback& on_ok,
                                const ErrorCallback& on_err) {
    const std::string base = normalizeBase(addon_url);
    const std::string url  = base + "/catalog/" + type + "/" + id + ".json";

    cpr::GetCallback(
        [on_ok, on_err](const cpr::Response& r) {
            if (r.error) {
                if (on_err) on_err(r.error.message, -1);
                return;
            }
            if (r.status_code != 200) {
                if (on_err) on_err("HTTP " + std::to_string(r.status_code), r.status_code);
                return;
            }
            try {
                if (on_ok) on_ok(parseCatalog(r.text));
            } catch (const std::exception& e) {
                if (on_err) on_err(std::string{"catalog parse: "} + e.what(), -2);
            }
        },
        cpr::Url{url}, cpr::Timeout{15000},
        cpr::Header{{"User-Agent", "Pepswitch/0.1 (Stremio)"}});
}

void AddonManager::fetchStreams(const std::string& addon_url, const std::string& type,
                                const std::string& id, const StreamCallback& on_ok,
                                const ErrorCallback& on_err) {
    const std::string base = normalizeBase(addon_url);
    const std::string url  = base + "/stream/" + type + "/" + id + ".json";

    cpr::GetCallback(
        [on_ok, on_err](const cpr::Response& r) {
            if (r.error) {
                if (on_err) on_err(r.error.message, -1);
                return;
            }
            if (r.status_code != 200) {
                if (on_err) on_err("HTTP " + std::to_string(r.status_code), r.status_code);
                return;
            }
            try {
                if (on_ok) on_ok(parseStreams(r.text));
            } catch (const std::exception& e) {
                if (on_err) on_err(std::string{"stream parse: "} + e.what(), -2);
            }
        },
        cpr::Url{url}, cpr::Timeout{15000},
        cpr::Header{{"User-Agent", "Pepswitch/0.1 (Stremio)"}});
}

// ---------------------------------------------------------------------------
// Parsers
// ---------------------------------------------------------------------------

static std::vector<std::string> jsonStringArray(const nlohmann::json& j) {
    std::vector<std::string> out;
    if (j.is_array()) {
        for (const auto& v : j) {
            if (v.is_string()) out.push_back(v.get<std::string>());
        }
    }
    return out;
}

StremioManifest AddonManager::parseManifest(const std::string& base_url, const std::string& body) {
    nlohmann::json j = nlohmann::json::parse(body);

    StremioManifest m;
    m.url         = base_url;
    m.id          = j.value("id", "");
    m.version     = j.value("version", "");
    m.name        = j.value("name", "");
    m.description = j.value("description", "");
    m.resources.clear();
    if (j.contains("resources")) {
        // resources can be either an array of strings OR an array of objects.
        for (const auto& r : j["resources"]) {
            if (r.is_string())
                m.resources.push_back(r.get<std::string>());
            else if (r.is_object() && r.contains("name"))
                m.resources.push_back(r["name"].get<std::string>());
        }
    }
    m.types = jsonStringArray(j.value("types", nlohmann::json::array()));

    if (j.contains("catalogs") && j["catalogs"].is_array()) {
        for (const auto& c : j["catalogs"]) {
            StremioCatalog cat;
            cat.id    = c.value("id", "");
            cat.type  = c.value("type", "");
            cat.name  = c.value("name", cat.id);
            cat.extra = c.contains("extra") ? c["extra"] : nlohmann::json::array();
            m.catalogs.push_back(std::move(cat));
        }
    }
    m.raw = std::move(j);
    return m;
}

std::vector<StremioMeta> AddonManager::parseCatalog(const std::string& body) {
    nlohmann::json j = nlohmann::json::parse(body);

    std::vector<StremioMeta> out;
    if (j.contains("metas") && j["metas"].is_array()) {
        out.reserve(j["metas"].size());
        for (const auto& it : j["metas"]) {
            StremioMeta m;
            m.id          = it.value("id", "");
            m.type        = it.value("type", "");
            m.name        = it.value("name", "");
            m.poster      = it.value("poster", "");
            m.background  = it.value("background", "");
            m.description = it.value("description", "");
            m.releaseInfo = it.value("releaseInfo", "");
            if (it.contains("imdbRating")) {
                if (it["imdbRating"].is_number())
                    m.imdbRating = it["imdbRating"].get<double>();
                else if (it["imdbRating"].is_string()) {
                    try {
                        m.imdbRating = std::stod(it["imdbRating"].get<std::string>());
                    } catch (...) {
                    }
                }
            }
            m.raw = it;
            if (!m.id.empty()) out.push_back(std::move(m));
        }
    }
    return out;
}

std::vector<StremioStream> AddonManager::parseStreams(const std::string& body) {
    nlohmann::json j = nlohmann::json::parse(body);

    std::vector<StremioStream> out;
    if (j.contains("streams") && j["streams"].is_array()) {
        out.reserve(j["streams"].size());
        for (const auto& it : j["streams"]) {
            StremioStream s;
            s.title    = it.value("title", "");
            s.name     = it.value("name", "");
            s.url      = it.value("url", "");
            s.ytId     = it.value("ytId", "");
            s.infoHash = it.value("infoHash", "");
            s.fileIdx  = it.value("fileIdx", -1);
            s.quality  = detectQuality(s.title.empty() ? s.name : s.title);
            s.raw      = it;
            out.push_back(std::move(s));
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Quality heuristics
// ---------------------------------------------------------------------------

std::string AddonManager::detectQuality(const std::string& title) {
    std::string upper;
    upper.reserve(title.size());
    for (unsigned char c : title) upper.push_back(static_cast<char>(std::toupper(c)));
    if (upper.find("2160P") != std::string::npos || upper.find("4K") != std::string::npos)
        return "4K";
    if (upper.find("1440P") != std::string::npos) return "1440p";
    if (upper.find("1080P") != std::string::npos) return "1080p";
    if (upper.find("720P") != std::string::npos) return "720p";
    if (upper.find("480P") != std::string::npos) return "480p";
    if (upper.find("CAM") != std::string::npos) return "CAM";
    return "";
}

const StremioStream* AddonManager::pickBestStream(const std::vector<StremioStream>& streams) {
    static const std::vector<std::string> order = {"4K", "1440p", "1080p", "720p", "480p", ""};
    for (const auto& q : order) {
        for (const auto& s : streams) {
            if (s.isPlayable() && s.quality == q) return &s;
        }
    }
    // Last resort: first playable.
    for (const auto& s : streams) {
        if (s.isPlayable()) return &s;
    }
    return nullptr;
}

}  // namespace pepswitch
