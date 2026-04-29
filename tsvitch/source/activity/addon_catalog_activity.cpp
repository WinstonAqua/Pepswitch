// Pepswitch — catalog browser activity implementation.

#include "activity/addon_catalog_activity.hpp"

#include <utility>

#include <borealis/core/application.hpp>
#include <borealis/core/thread.hpp>
#include <borealis/views/dialog.hpp>
#include <borealis/views/label.hpp>

#include "view/addon_cell.hpp"
#include "view/recycling_grid.hpp"
#include "activity/addon_streams_activity.hpp"

using brls::ControllerButton;

// ---------------------------------------------------------------------------
// Catalog data source
// ---------------------------------------------------------------------------

class DataSourceCatalog : public RecyclingGridDataSource {
public:
    DataSourceCatalog(std::string addon_url, std::vector<pepswitch::StremioMeta> metas)
        : addonUrl_(std::move(addon_url)), metas_(std::move(metas)) {}

    RecyclingGridItem* cellForRow(RecyclingGrid* recycler, size_t index) override {
        AddonMetaCell* cell = (AddonMetaCell*)recycler->dequeueReusableCell("Cell");
        cell->setMeta(metas_[index]);
        return cell;
    }

    size_t getItemCount() override { return metas_.size(); }

    void onItemSelected(RecyclingGrid* recycler, size_t index) override {
        const pepswitch::StremioMeta& meta = metas_[index];
        auto* activity = new AddonStreamsActivity(addonUrl_, meta);
        brls::Application::pushActivity(activity);
    }

    void clearData() override { metas_.clear(); }

private:
    std::string                          addonUrl_;
    std::vector<pepswitch::StremioMeta>  metas_;
};

// ---------------------------------------------------------------------------
// AddonCatalogActivity
// ---------------------------------------------------------------------------

AddonCatalogActivity::AddonCatalogActivity(pepswitch::StremioManifest manifest)
    : manifest_(std::move(manifest)) {}

void AddonCatalogActivity::onContentAvailable() {
    if (this->grid) {
        this->grid->registerCell("Cell", []() { return AddonMetaCell::create(); });
    }
    if (this->header) {
        this->header->setText(manifest_.name);
    }

    // Y — refresh
    this->registerAction(
        "Refresh", ControllerButton::BUTTON_Y,
        [this](brls::View*) {
            this->refreshCurrent();
            return true;
        },
        false);

    // RB — next catalog (cycle)
    this->registerAction(
        "Next catalog", ControllerButton::BUTTON_RB,
        [this](brls::View*) {
            if (manifest_.catalogs.empty()) return true;
            currentCatalog_ = (currentCatalog_ + 1) % manifest_.catalogs.size();
            this->loadCatalog(currentCatalog_);
            return true;
        },
        false);

    if (manifest_.catalogs.empty()) {
        if (this->grid) this->grid->setError("This add-on has no catalogs.");
        return;
    }
    this->loadCatalog(0);
}

void AddonCatalogActivity::refreshCurrent() { this->loadCatalog(currentCatalog_); }

void AddonCatalogActivity::loadCatalog(size_t index) {
    if (manifest_.catalogs.empty()) return;
    if (index >= manifest_.catalogs.size()) index = 0;
    currentCatalog_ = index;

    const auto& cat = manifest_.catalogs[index];
    if (this->header) {
        this->header->setText(manifest_.name + "  ·  " + cat.name);
    }
    if (this->grid) {
        this->grid->showSkeleton();
    }

    pepswitch::AddonManager::instance().fetchCatalog(
        manifest_.url, cat.type, cat.id,
        [this, addon_url = manifest_.url](std::vector<pepswitch::StremioMeta> metas) {
            brls::sync([this, addon_url, metas = std::move(metas)]() mutable {
                if (!this->grid) return;
                if (metas.empty()) {
                    this->grid->setEmpty("Empty catalog");
                    return;
                }
                this->grid->setDataSource(
                    new DataSourceCatalog(addon_url, std::move(metas)));
            });
        },
        [this](const std::string& msg, int code) {
            brls::sync([this, msg, code]() {
                if (this->grid)
                    this->grid->setError("Catalog error: " + msg + " (" +
                                         std::to_string(code) + ")");
            });
        });
}
