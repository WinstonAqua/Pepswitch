// Pepswitch — streams list activity implementation.
//
// Reuses the existing TsVitch LiveActivity / VideoView / MPV pipeline to play
// a Stremio HTTP stream. We synthesize a single-element LiveM3u8 channel list
// from the picked stream and call Intent::openLive — that is the same
// path the IPTV browser already uses, so we don't touch MPV at all.

#include "activity/addon_streams_activity.hpp"

#include <utility>

#include <borealis/core/application.hpp>
#include <borealis/core/thread.hpp>
#include <borealis/views/dialog.hpp>
#include <borealis/views/label.hpp>

#include "view/addon_cell.hpp"
#include "view/recycling_grid.hpp"

#include "utils/activity_helper.hpp"
#include "api/tsvitch/result/home_live_result.h"

using brls::ControllerButton;

// ---------------------------------------------------------------------------
// Streams data source
// ---------------------------------------------------------------------------

class DataSourceStreams : public RecyclingGridDataSource {
public:
    DataSourceStreams(pepswitch::StremioMeta meta, std::vector<pepswitch::StremioStream> streams)
        : meta_(std::move(meta)), streams_(std::move(streams)) {}

    RecyclingGridItem* cellForRow(RecyclingGrid* recycler, size_t index) override {
        AddonStreamCell* cell = (AddonStreamCell*)recycler->dequeueReusableCell("Cell");
        cell->setStream(streams_[index]);
        return cell;
    }

    size_t getItemCount() override { return streams_.size(); }

    void onItemSelected(RecyclingGrid* recycler, size_t index) override {
        const pepswitch::StremioStream& s = streams_[index];

        if (!s.isPlayable()) {
            auto* dialog = new brls::Dialog(
                "This stream type is not supported on Switch.\n"
                "(Magnet/torrent streams cannot be played in-app.)");
            dialog->addButton("OK", []() {});
            dialog->open();
            return;
        }

        // Synthesize a single-channel list and reuse the IPTV player path.
        tsvitch::LiveM3u8 ch;
        ch.id         = meta_.id;
        ch.chno       = "1";
        ch.title      = meta_.name + (s.quality.empty() ? "" : " · " + s.quality);
        ch.logo       = meta_.poster;
        ch.groupTitle = "Stremio";
        ch.url        = s.url.empty() ? ("https://www.youtube.com/watch?v=" + s.ytId) : s.url;

        std::vector<tsvitch::LiveM3u8> channels{ch};
        Intent::openLive(channels, 0, []() {});
    }

    void clearData() override { streams_.clear(); }

private:
    pepswitch::StremioMeta                meta_;
    std::vector<pepswitch::StremioStream> streams_;
};

// ---------------------------------------------------------------------------
// AddonStreamsActivity
// ---------------------------------------------------------------------------

AddonStreamsActivity::AddonStreamsActivity(std::string addon_url, pepswitch::StremioMeta meta)
    : addonUrl_(std::move(addon_url)), meta_(std::move(meta)) {}

void AddonStreamsActivity::onContentAvailable() {
    if (this->grid)
        this->grid->registerCell("Cell", []() { return AddonStreamCell::create(); });

    if (this->title) this->title->setText(meta_.name);
    if (this->subtitle) {
        std::string sub;
        if (!meta_.releaseInfo.empty()) sub += meta_.releaseInfo;
        if (meta_.imdbRating > 0.0) {
            if (!sub.empty()) sub += "  ·  ";
            sub += "IMDb " + std::to_string(meta_.imdbRating);
        }
        this->subtitle->setText(sub);
    }

    this->registerAction(
        "Refresh", ControllerButton::BUTTON_Y,
        [this](brls::View*) {
            this->refresh();
            return true;
        },
        false);

    // X — quick play of best stream
    this->registerAction(
        "Quick play (best)", ControllerButton::BUTTON_X,
        [this](brls::View*) {
            const pepswitch::StremioStream* best =
                pepswitch::AddonManager::pickBestStream(streams_);
            if (best) {
                this->playStream(*best);
            } else {
                brls::Application::notify("No playable stream.");
            }
            return true;
        },
        false);

    this->refresh();
}

void AddonStreamsActivity::refresh() {
    if (this->grid) this->grid->showSkeleton();
    pepswitch::AddonManager::instance().fetchStreams(
        addonUrl_, meta_.type, meta_.id,
        [this](std::vector<pepswitch::StremioStream> streams) {
            brls::sync([this, streams = std::move(streams)]() mutable {
                streams_ = streams;  // keep a copy for "quick play"
                if (!this->grid) return;
                if (streams.empty()) {
                    this->grid->setEmpty("No streams available.");
                    return;
                }
                this->grid->setDataSource(new DataSourceStreams(meta_, std::move(streams)));
            });
        },
        [this](const std::string& msg, int code) {
            brls::sync([this, msg, code]() {
                if (this->grid)
                    this->grid->setError("Stream error: " + msg + " (" +
                                         std::to_string(code) + ")");
            });
        });
}

void AddonStreamsActivity::playStream(const pepswitch::StremioStream& s) {
    if (!s.isPlayable()) {
        brls::Application::notify("Stream not playable.");
        return;
    }
    tsvitch::LiveM3u8 ch;
    ch.id         = meta_.id;
    ch.chno       = "1";
    ch.title      = meta_.name + (s.quality.empty() ? "" : " · " + s.quality);
    ch.logo       = meta_.poster;
    ch.groupTitle = "Stremio";
    ch.url        = s.url.empty() ? ("https://www.youtube.com/watch?v=" + s.ytId) : s.url;
    std::vector<tsvitch::LiveM3u8> channels{ch};
    Intent::openLive(channels, 0, []() {});
}
