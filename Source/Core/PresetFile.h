#pragma once

#include <string>
#include <vector>

namespace pixo
{

/** Image banks, each with its slot count and the folder under the media root
    that v114 read it from (PD114-ANALYSIS §2.3, §4). */
enum class ImageBank
{
    mix,    // 4 slots (+1 scratch slot for swaps, P4), images1024/
    pad,    // 12 slots, images1024/ (v114; the old preset files point to images800x600/)
    strip   // 4 slots, images_strip/
};

int bankSlotCount (ImageBank bank);
const char* bankImageFolder (ImageBank bank);
const char* bankPresetPrefix (ImageBank bank);  // "mixpreset", "padpreset", "stripreset"

/** The image file name of one preset line. Old files hold full paths
    (F:/Le_Pixophone/images1024/x.jpg); v114 expected bare names and added
    ".jpg". Both reduce to the file name, with ".jpg" added when there is no
    extension. Empty for blank lines. */
std::string presetEntryFileName (const std::string& line);

/** One file name per non-empty line, in slot order. */
std::vector<std::string> parsePresetFile (const std::string& text);

} // namespace pixo
