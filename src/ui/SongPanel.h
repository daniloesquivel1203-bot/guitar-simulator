#pragma once

#include "../engine/AudioEngine.h"
#include "TabView.h"
#include "TonePanels.h"

/** The loaded song. It outlives the panel, so a song keeps playing and stays
    loaded while other screens are open. */
struct SongSession
{
    tab::Song song;
    std::vector<tab::PlayedBar> order;
    juce::File file;
    int shownTrack = 0;
    std::vector<bool> trackOn;
    double speed = 1.0;

    /** True after this song was handed to the band. The jam-track panel uses
        the same band, so the song also checks the band still has it. */
    bool handedToBand = false;
    double lengthBeats = 0.0;

    bool isLoaded() const noexcept   { return ! song.isEmpty(); }
};

/** Songs: open a Guitar Pro or MusicXML file, watch its tab scroll, and play
    along with the other parts on the built-in instruments. */
class SongPanel final : public TonePanel,
                        private juce::Timer
{
public:
    SongPanel (AudioEngine&, SongSession&);
    ~SongPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshPower() override {}

    /** True while the band is playing a song, so the header can show it. */
    static bool isPlaying (AudioEngine&, const SongSession&);

private:
    void timerCallback() override;

    void chooseFile();
    void load (const juce::File&, bool quiet);
    void refreshSongControls();
    void rebuildTrackToggles();
    void sendToBand (bool restart);
    void togglePlay();
    bool bandHasSong() const;

    AudioEngine& engine;
    SongSession& session;

    juce::TextButton openButton { "Open song..." };
    juce::Label titleLabel;
    juce::Label showLabel { {}, "Show" };
    juce::ComboBox trackChoice;
    juce::Label speedLabel { {}, "Speed" };
    juce::ComboBox speedChoice;
    juce::TextButton playButton { "Play" };
    juce::Label statusLabel;
    juce::Label bandLabel { {}, "Band plays" };
    juce::OwnedArray<juce::ToggleButton> trackToggles;
    std::vector<int> toggleTrackIndex;

    TabView tabView;
    std::unique_ptr<juce::VBlankAttachment> vblank;
    std::unique_ptr<juce::FileChooser> chooser;
};
