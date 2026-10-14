#include "Media/MediaLibrary.h"

#include "Core/ImageOps.h"
#include "Render/RenderEngine.h"

namespace pixo
{
namespace
{

constexpr const char* mediaRootKey = "mediaRoot";

/** Decodes any JUCE-supported file into straight-alpha RGBA, top row first. */
std::shared_ptr<DecodedImage> decodeImage (const juce::File& file)
{
    auto image = juce::ImageFileFormat::loadFrom (file);

    if (! image.isValid())
        return nullptr;

    image = image.convertedToFormat (juce::Image::ARGB);
    const juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readOnly);

    auto result = std::make_shared<DecodedImage>();
    result->width = image.getWidth();
    result->height = image.getHeight();
    result->name = file.getFileName().toStdString();
    result->rgba.resize (static_cast<std::size_t> (result->width) * static_cast<std::size_t> (result->height) * 4);

    auto* out = result->rgba.data();

    for (int y = 0; y < result->height; ++y)
    {
        for (int x = 0; x < result->width; ++x)
        {
            auto pixel = *reinterpret_cast<const juce::PixelARGB*> (pixels.getPixelPointer (x, y));
            pixel.unpremultiply();     // JUCE stores premultiplied alpha; photos are opaque anyway
            *out++ = pixel.getRed();
            *out++ = pixel.getGreen();
            *out++ = pixel.getBlue();
            *out++ = pixel.getAlpha();
        }
    }

    return result;
}

} // namespace

struct MediaLibrary::Decoded
{
    juce::String fileName;
    std::shared_ptr<DecodedImage> image;    // nullptr: decoding failed
};

MediaLibrary::MediaLibrary (RenderEngine& engineIn, juce::PropertiesFile& settingsIn)
    : engine (engineIn), settings (settingsIn)
{
    for (auto bank : { ImageBank::mix, ImageBank::pad, ImageBank::strip })
    {
        slots[bank].resize (static_cast<std::size_t> (bankSlotCount (bank)));
        slotGenerations[bank].resize (static_cast<std::size_t> (bankSlotCount (bank)), 0);
    }
}

MediaLibrary::~MediaLibrary()
{
    decoder.removeAllJobs (true, 4000);
}

juce::File MediaLibrary::getMediaRoot() const
{
    auto path = settings.getValue (mediaRootKey);

    if (path.isEmpty())
        path = juce::SystemStats::getEnvironmentVariable ("PIXO_MEDIA_ROOT", {});

    return path.isNotEmpty() ? juce::File (path) : juce::File();
}

void MediaLibrary::setMediaRoot (const juce::File& folder)
{
    settings.setValue (mediaRootKey, folder.getFullPathName());
    settings.saveIfNeeded();
    setStatus ("Media root: " + folder.getFullPathName());
}

std::vector<MediaLibrary::SlotState>& MediaLibrary::slotsOf (ImageBank bank)
{
    return slots[bank];
}

MediaLibrary::SlotState MediaLibrary::getSlot (ImageBank bank, int slot) const
{
    const auto it = slots.find (bank);

    if (it == slots.end() || slot < 0 || slot >= static_cast<int> (it->second.size()))
        return {};

    return it->second[static_cast<std::size_t> (slot)];
}

void MediaLibrary::setStatus (const juce::String& message)
{
    statusMessage = message;
    juce::Logger::writeToLog ("Media: " + message);

    if (onChange)
        onChange();
}

void MediaLibrary::loadPreset (ImageBank bank, int number)
{
    const auto root = getMediaRoot();
    const auto presetName = juce::String (bankPresetPrefix (bank)) + juce::String (number);

    if (! root.isDirectory())
    {
        setStatus ("No media folder set - choose the old Le_Pixophone folder");
        return;
    }

    const auto presetFile = root.getChildFile ("presets").getChildFile (presetName + ".txt");

    if (! presetFile.existsAsFile())
    {
        setStatus (presetName + ": no such preset file");
        return;
    }

    const auto names = parsePresetFile (presetFile.loadFileAsString().toStdString());
    const auto folder = root.getChildFile (bankImageFolder (bank));

    if (bank == ImageBank::strip)
    {
        loadStripPreset (presetName, names, folder);
        return;
    }

    auto& bankSlots = slotsOf (bank);
    int missing = 0;

    for (int slot = 0; slot < static_cast<int> (bankSlots.size()) && slot < static_cast<int> (names.size()); ++slot)
    {
        auto& state = bankSlots[static_cast<std::size_t> (slot)];
        const auto generation = ++slotGenerations[bank][static_cast<std::size_t> (slot)];
        const auto file = folder.getChildFile (juce::String (names[static_cast<std::size_t> (slot)]));

        state.fileName = file.getFileName();

        if (! file.existsAsFile())
        {
            state.status = SlotStatus::missing;     // GEM kept the slot's previous picture
            ++missing;
            continue;
        }

        state.status = SlotStatus::loading;
        decodeInBackground (bank, slot, file, generation);
    }

    setStatus (presetName + ": " + juce::String (static_cast<int> (names.size())) + " image(s)"
               + (missing > 0 ? ", " + juce::String (missing) + " missing" : juce::String()));
}

void MediaLibrary::loadStripPreset (const juce::String& presetName, const std::vector<std::string>& names,
                                    const juce::File& folder)
{
    auto& bankSlots = slotsOf (ImageBank::strip);
    std::vector<juce::File> files;
    int missing = 0;

    for (std::size_t i = 0; i < bankSlots.size(); ++i)
    {
        auto& state = bankSlots[i];

        if (i >= names.size())
        {
            state = SlotState {};
            continue;
        }

        const auto file = folder.getChildFile (juce::String (names[i]));
        state.fileName = file.getFileName();
        state.status = file.existsAsFile() ? SlotStatus::loading : SlotStatus::missing;
        missing += file.existsAsFile() ? 0 : 1;
        files.push_back (file);
    }

    if (files.empty() || missing > 0)
    {
        // GEM kept the previous strip images; the port keeps the panorama.
        for (auto& state : bankSlots)
            if (state.status == SlotStatus::loading)
                state.status = SlotStatus::empty;

        setStatus (presetName + ": " + juce::String (missing) + " of " + juce::String (static_cast<int> (files.size()))
                   + " part(s) missing, previous strip kept");
        return;
    }

    const auto generation = ++slotGenerations[ImageBank::strip][0];
    const auto partCount = static_cast<int> (files.size());
    juce::WeakReference<MediaLibrary> weakThis (this);

    decoder.addJob ([weakThis, files, generation, partCount]
    {
        std::vector<std::shared_ptr<const DecodedImage>> parts;

        for (const auto& file : files)
            parts.push_back (decodeImage (file));

        std::shared_ptr<const DecodedImage> panorama;

        if (std::none_of (parts.begin(), parts.end(), [] (const auto& part) { return part == nullptr; }))
            panorama = std::make_shared<DecodedImage> (joinHorizontally (parts, portableMaxTextureSize));

        juce::MessageManager::callAsync ([weakThis, generation, partCount, panorama]
        {
            if (auto* self = weakThis.get())
                self->finishStrip (generation, partCount, panorama);
        });
    });

    setStatus (presetName + ": " + juce::String (partCount) + (partCount == 1 ? " panorama" : " parts, joined"));
}

void MediaLibrary::finishStrip (int generation, int partCount, std::shared_ptr<const DecodedImage> panorama)
{
    if (slotGenerations[ImageBank::strip][0] != generation)
        return;

    auto& bankSlots = slotsOf (ImageBank::strip);

    for (int i = 0; i < partCount && i < static_cast<int> (bankSlots.size()); ++i)
        bankSlots[static_cast<std::size_t> (i)].status = panorama != nullptr ? SlotStatus::loaded : SlotStatus::failed;

    if (panorama == nullptr)
    {
        setStatus ("Could not decode a strip part");
        return;
    }

    engine.setImage (ImageBank::strip, 0, panorama);
    setStatus ("Strip: " + juce::String (panorama->width) + " x " + juce::String (panorama->height) + " panorama");
}

void MediaLibrary::decodeInBackground (ImageBank bank, int slot, const juce::File& file, int generation)
{
    juce::WeakReference<MediaLibrary> weakThis (this);

    decoder.addJob ([weakThis, bank, slot, file, generation]
    {
        auto result = std::make_shared<Decoded>();
        result->fileName = file.getFileName();
        result->image = decodeImage (file);

        juce::MessageManager::callAsync ([weakThis, bank, slot, generation, result]
        {
            if (auto* self = weakThis.get())
                self->finishSlot (bank, slot, generation, result);
        });
    });
}

void MediaLibrary::finishSlot (ImageBank bank, int slot, int generation, std::shared_ptr<Decoded> result)
{
    if (slotGenerations[bank][static_cast<std::size_t> (slot)] != generation)
        return;     // a newer load replaced this slot meanwhile

    auto& state = slotsOf (bank)[static_cast<std::size_t> (slot)];

    if (result->image == nullptr)
    {
        state.status = SlotStatus::failed;
        setStatus ("Could not decode " + result->fileName);
        return;
    }

    state.status = SlotStatus::loaded;
    engine.setImage (bank, slot, result->image);

    if (onChange)
        onChange();
}

} // namespace pixo
