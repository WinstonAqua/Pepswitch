// Pepswitch — catalog browser activity.
// Shown when the user picks an add-on. Loads the first catalog declared in
// the manifest and presents a poster-grid of StremioMeta items.

#pragma once

#include <borealis/core/activity.hpp>
#include <borealis/core/bind.hpp>

#include "core/addon_manager.hpp"

class RecyclingGrid;

namespace brls {
class Label;
}

class AddonCatalogActivity : public brls::Activity {
public:
    CONTENT_FROM_XML_RES("activity/addon_catalog_activity.xml");

    explicit AddonCatalogActivity(pepswitch::StremioManifest manifest);
    ~AddonCatalogActivity() override = default;

    void onContentAvailable() override;

    // Refresh the active catalog from the network.
    void refreshCurrent();

private:
    void loadCatalog(size_t index);

    pepswitch::StremioManifest manifest_;
    size_t                     currentCatalog_ = 0;

    BRLS_BIND(brls::Label,   header, "addon/catalog/header");
    BRLS_BIND(RecyclingGrid, grid,   "addon/catalog/grid");
};
