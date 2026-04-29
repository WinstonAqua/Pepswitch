// Pepswitch — streams list activity.
// Shown when the user picks a piece of content from a Stremio catalog.
// Lists every available stream; A button opens the best-quality URL in
// the existing TsVitch VideoView/MPV pipeline.

#pragma once

#include <borealis/core/activity.hpp>
#include <borealis/core/bind.hpp>

#include "core/addon_manager.hpp"

class RecyclingGrid;

namespace brls {
class Label;
}

class AddonStreamsActivity : public brls::Activity {
public:
    CONTENT_FROM_XML_RES("activity/addon_streams_activity.xml");

    explicit AddonStreamsActivity(std::string addon_url, pepswitch::StremioMeta meta);
    ~AddonStreamsActivity() override = default;

    void onContentAvailable() override;

    // Re-fetch the streams list.
    void refresh();

    // Open the chosen stream via VideoView/MPV.
    void playStream(const pepswitch::StremioStream& stream);

private:
    std::string                          addonUrl_;
    pepswitch::StremioMeta               meta_;
    std::vector<pepswitch::StremioStream> streams_;

    BRLS_BIND(brls::Label,   title,    "addon/streams/title");
    BRLS_BIND(brls::Label,   subtitle, "addon/streams/subtitle");
    BRLS_BIND(RecyclingGrid, grid,     "addon/streams/grid");
};
