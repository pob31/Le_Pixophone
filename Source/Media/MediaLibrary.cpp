#include "Media/MediaLibrary.h"

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
