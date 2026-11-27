#pragma once

#include "../engine/AudioEngine.h"
#include "TonePanels.h"

/** Jam tracks: pick a style, a key and a chord progression, and a backing band
    plays it on a loop while you play over it. The band keeps playing when the
    panel is closed. */
class JamPanel final : public TonePanel,
                       private juce::Timer
{
public:
    explicit JamPanel (AudioEngine& engineToUse);
    ~JamPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshPower() override {}

    /** Restores the band's mix from the settings file. Call once at start-up. */
    static void applySettings (AudioEngine&);

    static bool isBusy (AudioEngine& engine)   { return engine.getPractice().getJamTrack().isPlaying(); }

private:
    /** The progression drawn as a chord chart, four bars to a row. Click a
        chord to change it. */
    class ChordChart final : public juce::Component,
                             public juce::SettableTooltipClient
    {
    public:
        explicit ChordChart (JamPanel& ownerToUse) : owner (ownerToUse) {}

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

        /** Pieces of a chord's box: a chord that crosses the end of a row is drawn in two. */
        struct Piece
        {
            int slot;   // -1 for the add box
            juce::Rectangle<float> area;
            bool first;
        };

        std::vector<Piece> layoutPieces() const;

        int playingSlot = -1;
        double playingBeat = -1.0;

    private:
        int slotAt (juce::Point<float>) const;

        JamPanel& owner;
        int hoverSlot = -2;
    };

    void timerCallback() override;

    void loadSettings();
    void saveProgression();
    void rebuild();
    void refreshTransport();
    void refreshInfo();
    void refreshProgressionBox();

    void chooseStyle (int index);
    void chooseMode (jam::Mode);
    void choosePreset (int index);
    void showChordMenu (int slot, juce::Rectangle<int> screenArea);
    void addChord();

    jam::Key key;
    jam::Style style = jam::Style::rock;
    jam::Progression progression;

    AudioEngine& engine;
    JamTrack& track;
    TapTempo tapTempo;

    juce::Rectangle<int> setupArea, chartArea, mixArea;

    // Setup
    juce::TextButton playButton { "Play" };
    juce::ToggleButton countIn { "Count in one bar" };
    juce::Slider bpm;
    juce::TextButton tapButton { "Tap" };
    juce::Label styleLabel { {}, "Style" }, keyLabel { {}, "Key" }, progressionLabel { {}, "Chords" };
    juce::ComboBox styleBox, rootBox, modeBox, progressionBox;
    juce::Label styleNote;

    // Chords
    ChordChart chart { *this };
    juce::Label nowPlaying;
    juce::Label scales;

    // Mix
    std::array<juce::ToggleButton, jam::numInstruments> partButtons;
    std::array<juce::Slider, jam::numInstruments> partLevels;
    juce::Label masterLabel { {}, "Band" };
    juce::Slider master;
    juce::Label mixNote;

    std::vector<int> presetIndexes;   // progression box item -> getProgressionPresets() index
    int shownSlot = -2;
};
