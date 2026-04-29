// Pepswitch - Stremio add-on integration for the Switch homebrew media center.
//
// This module owns the lifecycle of Stremio add-ons: fetching their manifest,
// resolving catalogs and streams, and persisting the user-added add-on URLs
// to the SD card. Networking goes through the existing cpr / libcurl stack
// already shipped by TsVitch (see tsvitch/util/http.hpp); JSON parsing is
// delegated to nlohmann::json.
//
// Storage layout (Switch):
//   /switch/stremionx/addons.json
//
// On non-Switch builds the same filename is used inside ProgramConfig::getConfigDir()
// so the desktop build still loads/saves the list for development.

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace pepswitch {

// ---------------------------------------------------------------------------
// Stremio data structures (subset of the official add-on protocol).
// Only the fields the UI actually consumes are modeled — anything else is
// retained as raw JSON in `extra` so future features can introspect it.
// ---------------------------------------------------------------------------

struct StremioCatalog {
    std::string id;       // e.g. "top"
    std::string type;     // "movie" | "series" | "tv" | "channel" | ...
    std::string name;     // human-readable
    nlohmann::json extra; // optional filters / pagination hints
};

struct StremioManifest {
    std::string url;          // base URL the manifest came from (no trailing slash)
    std::string id;           // reverse-DNS add-on id
    std::string version;      // semver
    std::string name;         // display name
    std::string description;  // short description
    std::vector<std::string> resources; // "catalog", "meta", "stream", "subtitles", ...
    std::vector<std::string> types;     // "movie", "series", ...
    std::vector<StremioCatalog> catalogs;
    nlohmann::json raw;       // full parsed manifest, for fields we didn't model
};

// One catalog entry — a movie/series/etc card.
struct StremioMeta {
    std::string id;          // e.g. "tt0133093"
    std::string type;        // "movie" | "series" | ...
    std::string name;        // title
    std::string poster;      // primary poster URL
    std::string background;  // optional landscape image
    std::string description; // synopsis
    std::string releaseInfo; // year / "1999-2003"
    double imdbRating = 0.0;
    nlohmann::json raw;
};

// One playable stream returned by /{type}/stream/{id}.json.
struct StremioStream {
    std::string title;       // human label, often "1080p WEB-DL ..."
    std::string name;        // add-on-provided name (often the source)
    std::string url;         // direct https URL (or empty if magnet/etc.)
    std::string ytId;        // youtube id (if any)
    std::string infoHash;    // bittorrent info hash (if any)
    int fileIdx = -1;        // bittorrent file index (if any)
    std::string quality;     // best-effort quality label parsed from title (1080p/720p/4K/...)
    nlohmann::json raw;

    // True if this stream can be opened by libmpv directly.
    bool isPlayable() const { return !url.empty() || !ytId.empty(); }
};

// User-added add-on entry, persisted to disk.
struct AddonEntry {
    std::string url;       // base URL (e.g. "https://v3-cinemeta.strem.io")
    std::string name;      // cached display name (from last successful manifest)
    std::string version;   // cached version
    bool enabled = true;
};

// ---------------------------------------------------------------------------
// AddonManager
// ---------------------------------------------------------------------------
// Singleton that owns:
//   * the list of user-added add-on URLs (persistent on SD card)
//   * an in-memory cache of fetched manifests
//   * async helpers around cpr (libcurl) for manifest/catalog/stream calls
//
// All public methods are thread-safe. Callbacks are dispatched on the cpr
// worker thread — UI code MUST marshal back to the brls main thread with
// brls::sync(...) before touching views.
// ---------------------------------------------------------------------------

class AddonManager {
public:
    using ManifestCallback = std::function<void(StremioManifest)>;
    using CatalogCallback  = std::function<void(std::vector<StremioMeta>)>;
    using StreamCallback   = std::function<void(std::vector<StremioStream>)>;
    using ErrorCallback    = std::function<void(const std::string& message, int code)>;

    static AddonManager& instance();

    // -- Persistence -------------------------------------------------------

    // Path to the persisted addons.json on disk.
    static std::string getStoragePath();

    // Load addons.json from disk. Safe to call multiple times. Returns true
    // if the file existed and was parsed cleanly.
    bool load();

    // Persist the current add-on list to disk. Returns true on success.
    bool save() const;

    // -- Add-on list management -------------------------------------------

    std::vector<AddonEntry> getAddons() const;
    bool                    hasAddon(const std::string& url) const;
    bool                    addAddon(const std::string& url, const std::string& name = "",
                                     const std::string& version = "");
    bool                    removeAddon(const std::string& url);
    bool                    setEnabled(const std::string& url, bool enabled);

    // -- Network operations ------------------------------------------------

    // Fetch /manifest.json for the given base URL. Resolves trailing slashes
    // and a literal "manifest.json" suffix automatically.
    void fetchManifest(const std::string& url, const ManifestCallback& on_ok,
                       const ErrorCallback& on_err = nullptr);

    // Synchronous manifest fetch — blocks the calling thread. Used for tests
    // and one-shot CLI tooling, NOT for UI code.
    bool fetchManifestSync(const std::string& url, StremioManifest& out, std::string& err);

    // Fetch a catalog: /catalog/{type}/{id}.json (or with extra params).
    void fetchCatalog(const std::string& addon_url, const std::string& type, const std::string& id,
                      const CatalogCallback& on_ok, const ErrorCallback& on_err = nullptr);

    // Fetch streams for a single piece of content: /stream/{type}/{id}.json.
    void fetchStreams(const std::string& addon_url, const std::string& type, const std::string& id,
                      const StreamCallback& on_ok, const ErrorCallback& on_err = nullptr);

    // -- Parsing helpers (exposed for tests) ------------------------------

    static StremioManifest parseManifest(const std::string& base_url, const std::string& body);
    static std::vector<StremioMeta>   parseCatalog(const std::string& body);
    static std::vector<StremioStream> parseStreams(const std::string& body);

    // Pick the "best" stream from a list — currently prefers highest detected
    // quality among entries with a direct URL.
    static const StremioStream* pickBestStream(const std::vector<StremioStream>& streams);

private:
    AddonManager();
    ~AddonManager() = default;
    AddonManager(const AddonManager&)            = delete;
    AddonManager& operator=(const AddonManager&) = delete;

    static std::string normalizeBase(const std::string& url);
    static std::string detectQuality(const std::string& title);

    mutable std::mutex      mutex_;
    std::vector<AddonEntry> addons_;
    std::atomic<bool>       loaded_{false};
};

}  // namespace pepswitch
