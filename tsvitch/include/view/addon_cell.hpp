// Pepswitch — recycling-grid cells for the Stremio add-on browser.
//
// Three cells live here so they can share the same translation unit and the
// same auto-generated factories used by RecyclingGrid:
//   * AddonCell        — one row in the "Add-ons" tab
//   * AddonMetaCell    — one poster card in the catalog grid
//   * AddonStreamCell  — one row in the streams list

#pragma once

#include <borealis/views/box.hpp>
#include <borealis/views/image.hpp>
#include <borealis/views/label.hpp>

#include "view/recycling_grid.hpp"
#include "core/addon_manager.hpp"

class TextBox;

class AddonCell : public RecyclingGridItem {
public:
    AddonCell();
    ~AddonCell() override = default;

    void setEntry(const pepswitch::AddonEntry& entry);
    const pepswitch::AddonEntry& getEntry() const { return entry_; }
    static AddonCell* create();

private:
    pepswitch::AddonEntry entry_;
    BRLS_BIND(brls::Label, name,    "addon/cell/name");
    BRLS_BIND(brls::Label, url,     "addon/cell/url");
    BRLS_BIND(brls::Label, version, "addon/cell/version");
    BRLS_BIND(brls::Label, status,  "addon/cell/status");
};

class AddonMetaCell : public RecyclingGridItem {
public:
    AddonMetaCell();
    ~AddonMetaCell() override = default;

    void prepareForReuse() override;
    void cacheForReuse() override;

    void setMeta(const pepswitch::StremioMeta& meta);
    const pepswitch::StremioMeta& getMeta() const { return meta_; }

    static AddonMetaCell* create();

private:
    pepswitch::StremioMeta meta_;
    BRLS_BIND(brls::Image, picture, "addon/meta/picture");
    BRLS_BIND(TextBox,     title,   "addon/meta/title");
    BRLS_BIND(brls::Label, year,    "addon/meta/year");
};

class AddonStreamCell : public RecyclingGridItem {
public:
    AddonStreamCell();
    ~AddonStreamCell() override = default;

    void setStream(const pepswitch::StremioStream& s);
    static AddonStreamCell* create();

private:
    BRLS_BIND(brls::Label, quality, "addon/stream/quality");
    BRLS_BIND(brls::Label, title,   "addon/stream/title");
    BRLS_BIND(brls::Label, source,  "addon/stream/source");
    BRLS_BIND(brls::Label, badge,   "addon/stream/badge");
};
