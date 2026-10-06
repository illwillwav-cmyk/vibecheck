#include "BrandAssets.h"

#include "Theme.h"

#include <BinaryData.h>

namespace mbs
{
juce::Image seal()
{
    return isDarkTheme()
             ? juce::ImageCache::getFromMemory (BinaryData::seal_paper_png, BinaryData::seal_paper_pngSize)
             : juce::ImageCache::getFromMemory (BinaryData::seal_ink_png, BinaryData::seal_ink_pngSize);
}

juce::Image wordmark()
{
    return isDarkTheme()
             ? juce::ImageCache::getFromMemory (BinaryData::wordmark_paper_png, BinaryData::wordmark_paper_pngSize)
             : juce::ImageCache::getFromMemory (BinaryData::wordmark_ink_png, BinaryData::wordmark_ink_pngSize);
}
} // namespace mbs
