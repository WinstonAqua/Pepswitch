// Pepswitch — HomeAddons fragment.

#include "fragment/home_addons.hpp"

#include <borealis/core/application.hpp>
#include <borealis/core/i18n.hpp>
#include <borealis/core/ime.hpp>
#include <borealis/core/thread.hpp>
#include <borealis/views/dialog.hpp>

#include "core/addon_manager.hpp"
#include "view/addon_cell.hpp"
#include "view/recycling_grid.hpp"
#include "activity/addon_catalog_activity.hpp"

using brls::ControllerButton;

// ---------------------------------------------------------------------------
// Recycling-grid data source
// ---------------------------------------------------------------------------

class DataSourceAddons : public RecyclingGridDataSource {
public:
    explicit DataSourceAddons(std::vector<pepswitch::AddonEntry> entries)
        : entries_(std::move(entries)) {}

    RecyclingGridItem* cellForRow(RecyclingGrid* recycler, size_t index) override {
        AddonCell* cell = (AddonCell*)recycler->dequeueReusableCell("Cell");
        cell->setEntry(entries_[index]);
        return cell;
    }

    size_t getItemCount() override { return entries_.size(); }

    void onItemSelected(RecyclingGrid* recycler, size_t index) override {
        // Fetch manifest then push the catalog activity.
        const pepswitch::AddonEntry entry = entries_[index];

        brls::Application::notify("Loading " + entry.name + "...");
        pepswitch::AddonManager::instance().fetchManifest(
            entry.url,
            [entry](pepswitch::StremioManifest manifest) {
                brls::sync([entry, manifest = std::move(manifest)]() mutable {
                    // Cache name/version for next time.
                    pepswitch::AddonManager::instance().addAddon(entry.url, manifest.name,
                                                                manifest.version);
                    auto* activity = new AddonCatalogActivity(std::move(manifest));
                    brls::Application::pushActivity(activity);
                });
            },
            [](const std::string& msg, int code) {
                brls::sync([msg, code]() {
                    brls::Application::notify("Manifest error: " + msg + " (" +
                                              std::to_string(code) + ")");
                });
            });
    }

    void clearData() override { entries_.clear(); }

    const pepswitch::AddonEntry* at(size_t index) const {
        if (index >= entries_.size()) return nullptr;
        return &entries_[index];
    }

private:
    std::vector<pepswitch::AddonEntry> entries_;
};

// ---------------------------------------------------------------------------
// HomeAddons
// ---------------------------------------------------------------------------

HomeAddons::HomeAddons() {
    this->inflateFromXMLRes("xml/fragment/home_addons.xml");

    this->recyclingGrid->registerCell("Cell", []() { return AddonCell::create(); });

    // X — add a new add-on
    this->registerAction(
        "Add", ControllerButton::BUTTON_X,
        [this](brls::View*) {
            this->promptAddAddon();
            return true;
        },
        false);

    // Y — refresh manifests
    this->registerAction(
        "Refresh", ControllerButton::BUTTON_Y,
        [this](brls::View*) {
            this->refreshAllManifests();
            return true;
        },
        false);

    // RB — remove focused
    this->registerAction(
        "Remove", ControllerButton::BUTTON_RB,
        [this](brls::View*) {
            this->removeFocused();
            return true;
        },
        false);

    this->rebuildList();
}

HomeAddons::~HomeAddons() = default;

brls::View* HomeAddons::create() { return new HomeAddons(); }

void HomeAddons::onCreate() { this->rebuildList(); }

void HomeAddons::onShow() { this->rebuildList(); }

void HomeAddons::rebuildList() {
    auto entries = pepswitch::AddonManager::instance().getAddons();
    this->recyclingGrid->setDataSource(new DataSourceAddons(std::move(entries)));
}

// ---------------------------------------------------------------------------
// Add new — on-screen keyboard
// ---------------------------------------------------------------------------

void HomeAddons::promptAddAddon() {
    auto* ime = brls::Application::getPlatform()->getImeManager();
    if (!ime) {
        brls::Logger::error("HomeAddons: no IME manager");
        return;
    }

    ime->openForText(
        [this](std::string url) {
            if (url.empty()) return;

            brls::Application::notify("Fetching manifest...");
            pepswitch::AddonManager::instance().fetchManifest(
                url,
                [this, url](pepswitch::StremioManifest manifest) {
                    brls::sync([this, url, manifest = std::move(manifest)]() {
                        pepswitch::AddonManager::instance().addAddon(url, manifest.name,
                                                                    manifest.version);
                        brls::Application::notify("Added: " + manifest.name);
                        this->rebuildList();
                    });
                },
                [](const std::string& msg, int code) {
                    brls::sync([msg, code]() {
                        auto* dialog = new brls::Dialog(
                            "Failed to fetch manifest:\n" + msg + " (" + std::to_string(code) + ")");
                        dialog->addButton("OK", []() {});
                        dialog->open();
                    });
                });
        },
        "Stremio add-on URL", "https://...", 256, "https://", 0);
}

// ---------------------------------------------------------------------------
// Refresh
// ---------------------------------------------------------------------------

void HomeAddons::refreshAllManifests() {
    auto& mgr     = pepswitch::AddonManager::instance();
    auto  entries = mgr.getAddons();
    if (entries.empty()) {
        brls::Application::notify("No add-ons installed.");
        return;
    }

    brls::Application::notify("Refreshing " + std::to_string(entries.size()) + " add-on(s)...");
    for (const auto& e : entries) {
        mgr.fetchManifest(
            e.url,
            [url = e.url](pepswitch::StremioManifest m) {
                pepswitch::AddonManager::instance().addAddon(url, m.name, m.version);
            },
            [url = e.url](const std::string& msg, int code) {
                brls::Logger::warning("Refresh failed for {}: {} (code {})", url, msg, code);
            });
    }

    // Optimistic local refresh — manifest fetches will retroactively update names/versions.
    this->rebuildList();
}

// ---------------------------------------------------------------------------
// Remove focused
// ---------------------------------------------------------------------------

void HomeAddons::removeFocused() {
    auto* item = dynamic_cast<AddonCell*>(this->recyclingGrid->getFocusedItem());
    if (!item) return;

    const std::string url = item->getEntry().url;
    if (url.empty()) return;

    auto* dialog = new brls::Dialog("Remove this add-on?");
    dialog->addButton("Cancel", []() {});
    dialog->addButton("Remove", [this, url]() {
        pepswitch::AddonManager::instance().removeAddon(url);
        this->rebuildList();
        brls::Application::notify("Removed.");
    });
    dialog->open();
}

void HomeAddons::openFocusedCatalog() {
    // RecyclingGrid invokes onItemSelected on A press, which already opens the catalog.
    // Method retained for completeness / future direct invocation paths.
}
