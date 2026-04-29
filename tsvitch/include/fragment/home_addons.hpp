// Pepswitch — "Add-ons" tab.
//
// Shows the list of user-added Stremio add-ons. Buttons:
//   A (BUTTON_A)  → open catalog browser for the focused add-on
//   B (BUTTON_B)  → back / leave the tab (handled globally)
//   X (BUTTON_X)  → add new add-on (on-screen keyboard for URL)
//   Y (BUTTON_Y)  → refresh: re-fetch every manifest
//   RB (BUTTON_RB) → remove the focused add-on
//
// Note: the user spec mentioned the "+" button, but on Switch "+" = BUTTON_START
// which TsVitch already wires globally to Settings. We map Add to X to avoid
// stealing that global shortcut.

#pragma once

#include "view/auto_tab_frame.hpp"
#include "core/addon_manager.hpp"

class RecyclingGrid;

class HomeAddons : public AttachedView {
public:
    HomeAddons();
    ~HomeAddons() override;

    static View* create();

    void onCreate() override;
    void onShow() override;

    // Re-render the recycling grid from AddonManager state.
    void rebuildList();

    // Prompt the user for a URL via the on-screen keyboard, fetch the
    // manifest, and persist on success.
    void promptAddAddon();

    // Re-fetch every manifest in the background and refresh cached metadata.
    void refreshAllManifests();

    // Remove the currently focused entry.
    void removeFocused();

    // Open the catalog browser for the focused add-on.
    void openFocusedCatalog();

private:
    BRLS_BIND(RecyclingGrid, recyclingGrid, "home/addons/recyclingGrid");
};
