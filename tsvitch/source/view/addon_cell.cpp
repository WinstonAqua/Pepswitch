// Pepswitch — recycling-grid cell implementations.

#include "view/addon_cell.hpp"

#include "view/text_box.hpp"
#include "utils/image_helper.hpp"

// ---------------------------------------------------------------------------
// AddonCell — one row in the add-ons list
// ---------------------------------------------------------------------------

AddonCell::AddonCell() { this->inflateFromXMLRes("xml/views/addon_cell.xml"); }

void AddonCell::setEntry(const pepswitch::AddonEntry& entry) {
    entry_ = entry;
    this->name->setText(entry.name.empty() ? entry.url : entry.name);
    this->url->setText(entry.url);
    this->version->setText(entry.version.empty() ? std::string{"version: ?"}
                                                 : "v" + entry.version);
    this->status->setText(entry.enabled ? "ON" : "OFF");
}

AddonCell* AddonCell::create() { return new AddonCell(); }

// ---------------------------------------------------------------------------
// AddonMetaCell — poster card for the catalog grid
// ---------------------------------------------------------------------------

AddonMetaCell::AddonMetaCell() { this->inflateFromXMLRes("xml/views/addon_meta_cell.xml"); }

void AddonMetaCell::prepareForReuse() {
    if (!meta_.poster.empty() && this->picture) {
        ImageHelper::with(this->picture)->load(meta_.poster);
    }
}

void AddonMetaCell::cacheForReuse() {
    if (this->picture) ImageHelper::clear(this->picture);
}

void AddonMetaCell::setMeta(const pepswitch::StremioMeta& meta) {
    meta_ = meta;
    if (this->title)   this->title->setText(meta.name);
    if (this->year)    this->year->setText(meta.releaseInfo);
    if (this->picture && !meta.poster.empty())
        ImageHelper::with(this->picture)->load(meta.poster);
}

AddonMetaCell* AddonMetaCell::create() { return new AddonMetaCell(); }

// ---------------------------------------------------------------------------
// AddonStreamCell — one row in the streams list
// ---------------------------------------------------------------------------

AddonStreamCell::AddonStreamCell() { this->inflateFromXMLRes("xml/views/addon_stream_cell.xml"); }

void AddonStreamCell::setStream(const pepswitch::StremioStream& s) {
    this->quality->setText(s.quality.empty() ? std::string{"AUTO"} : s.quality);
    this->title->setText(s.title.empty() ? s.name : s.title);
    this->source->setText(s.name);
    if (!s.url.empty())
        this->badge->setText("HTTP");
    else if (!s.ytId.empty())
        this->badge->setText("YT");
    else if (!s.infoHash.empty())
        this->badge->setText("BT");
    else
        this->badge->setText("?");
}

AddonStreamCell* AddonStreamCell::create() { return new AddonStreamCell(); }
