#include "Core/PresetFile.h"

#include <sstream>

namespace pixo
{

int bankSlotCount (ImageBank bank)
{
    switch (bank)
    {
        case ImageBank::mix:   return 4;
        case ImageBank::pad:   return 12;
        case ImageBank::strip: return 4;
    }

    return 0;
}

const char* bankImageFolder (ImageBank bank)
{
    switch (bank)
    {
        case ImageBank::mix:   return "images1024";
        case ImageBank::pad:   return "images1024";
        case ImageBank::strip: return "images_strip";
    }

    return "";
}

const char* bankPresetPrefix (ImageBank bank)
{
    switch (bank)
    {
        case ImageBank::mix:   return "mixpreset";
        case ImageBank::pad:   return "padpreset";
        case ImageBank::strip: return "stripreset";
    }

    return "";
}

std::string presetEntryFileName (const std::string& line)
{
    const auto first = line.find_first_not_of (" \t\r\n");

    if (first == std::string::npos)
        return {};

    const auto last = line.find_last_not_of (" \t\r\n;");
    auto entry = line.substr (first, last - first + 1);

    const auto slash = entry.find_last_of ("/\\");

    if (slash != std::string::npos)
        entry = entry.substr (slash + 1);

    if (entry.find ('.') == std::string::npos)
        entry += ".jpg";

    return entry;
}

std::vector<std::string> parsePresetFile (const std::string& text)
{
    std::vector<std::string> names;
    std::istringstream stream (text);
    std::string line;

    while (std::getline (stream, line))
    {
        auto name = presetEntryFileName (line);

        if (! name.empty())
            names.push_back (std::move (name));
    }

    return names;
}

} // namespace pixo
