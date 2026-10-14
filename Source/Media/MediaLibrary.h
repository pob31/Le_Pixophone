#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_graphics/juce_graphics.h>

#include <array>
#include <functional>
#include <map>

#include "Core/DecodedImage.h"
#include "Core/PresetFile.h"

namespace pixo
{

class RenderEngine;

/** The image banks on disk: media root, preset files, decoding, upload
    (PD114-ANALYSIS §2.3, §4).

    The media root is the old project folder layout: `presets/`,
    `images1024/`, `images_strip/`, ... It is kept in the settings; when
    unset, the PIXO_MEDIA_ROOT environment variable is used.

    Loading a preset reads `presets/<prefix><n>.txt` and decodes each image on
    a worker thread, then hands it to the render engine. As in GEM, a missing
    or unreadable image leaves that slot's previous picture in place.

    STRIP presets (D25) list one panorama or several parts; the parts are
    joined left to right into one panorama. If any part is missing, the
    previous panorama stays.
    Message thread only, except where noted. */
class MediaLibrary final
{
public:
    MediaLibrary (RenderEngine& engine, juce::PropertiesFile& settings);
    ~MediaLibrary();

    juce::File getMediaRoot() const;
    void setMediaRoot (const juce::File& folder);

    /** Starts loading preset `number` of a bank (see class notes). */
    void loadPreset (ImageBank bank, int number);

    enum class SlotStatus
    {
        empty,
        loading,
        loaded,
        missing,    // not found under the bank folder: previous picture kept
        failed      // found but not decodable: previous picture kept
    };

    struct SlotState
    {
        juce::String fileName;
        SlotStatus status = SlotStatus::empty;
    };

    SlotState getSlot (ImageBank bank, int slot) const;
    juce::String getStatusMessage() const { return statusMessage; }

    /** Called on the message thread whenever slots or the status change. */
    std::function<void()> onChange;

private:
    struct Decoded;

    void decodeInBackground (ImageBank bank, int slot, const juce::File& file, int generation);
    void finishSlot (ImageBank bank, int slot, int generation, std::shared_ptr<Decoded> result);
    void loadStripPreset (const juce::String& presetName, const std::vector<std::string>& names, const juce::File& folder);
    void finishStrip (int generation, int partCount, std::shared_ptr<const DecodedImage> panorama);
    void setStatus (const juce::String& message);
    std::vector<SlotState>& slotsOf (ImageBank bank);

    RenderEngine& engine;
    juce::PropertiesFile& settings;
    juce::String statusMessage;

    std::map<ImageBank, std::vector<SlotState>> slots;
    std::map<ImageBank, std::vector<int>> slotGenerations;  // drops results of superseded loads

    juce::ThreadPool decoder { juce::ThreadPoolOptions{}.withNumberOfThreads (2).withThreadName ("Pixophone decoder") };

    JUCE_DECLARE_WEAK_REFERENCEABLE (MediaLibrary)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MediaLibrary)
};

} // namespace pixo
