#include "TremoloProcessor.h"

#include <Accelerate/Accelerate.h>

namespace wright
{
namespace
{
constexpr int wavetableSize = 2048;
constexpr double rateHz = 5.0;
}

TremoloProcessor::TremoloProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("In", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Out", juce::AudioChannelSet::stereo(), true))
{
    addParameter (depth = new juce::AudioParameterFloat ({ "depth", 1 }, "Depth", 0.0f, 1.0f, 0.6f));
    buildWavetable();
}

void TremoloProcessor::buildWavetable()
{
    wavetable.resize ((std::size_t) wavetableSize + 1);

    for (int i = 0; i <= wavetableSize; ++i)
        wavetable[(std::size_t) i] = (float) (0.5 + 0.5 * std::sin (juce::MathConstants<double>::twoPi
                                                                   * (double) i / (double) wavetableSize));
}

void TremoloProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    tableIncrement = (double) wavetableSize * rateHz / sampleRate;
    tablePosition = 0.0;
    gainScratch.assign ((std::size_t) juce::jmax (1, samplesPerBlock), 0.0f);
}

void TremoloProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();

    if ((int) gainScratch.size() < numSamples)
        gainScratch.assign ((std::size_t) numSamples, 0.0f);

    const auto depthAmount = depth->get();

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto index = (int) tablePosition;
        const auto fraction = (float) (tablePosition - (double) index);
        const auto a = wavetable[(std::size_t) index];
        const auto b = wavetable[(std::size_t) index + 1];

        gainScratch[(std::size_t) sample] = 1.0f - depthAmount * (1.0f - (a + (b - a) * fraction));

        tablePosition += tableIncrement;

        if (tablePosition >= (double) wavetableSize)
            tablePosition -= (double) wavetableSize;
    }

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        vDSP_vmul (buffer.getReadPointer (channel), 1, gainScratch.data(), 1,
                   buffer.getWritePointer (channel), 1, (vDSP_Length) numSamples);
}

/** A small designed editor rather than the generic parameter list. */
class SwayEditor final : public juce::AudioProcessorEditor
{
public:
    explicit SwayEditor (TremoloProcessor& owner)
        : AudioProcessorEditor (owner), processor (owner)
    {
        depthSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        depthSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
        depthSlider.setRange (0.0, 1.0, 0.001);
        depthSlider.setValue (processor.getDepthParameter().get(), juce::dontSendNotification);
        depthSlider.onValueChange = [this] { processor.getDepthParameter().setValueNotifyingHost ((float) depthSlider.getValue()); };
        addAndMakeVisible (depthSlider);

        setSize (240, 180);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff101418));
        g.setColour (juce::Colour (0xff6fcf97));
        g.setFont (juce::FontOptions (16.0f));
        g.drawText ("SWAY", getLocalBounds().removeFromTop (34), juce::Justification::centred);
    }

    void resized() override
    {
        depthSlider.setBounds (getLocalBounds().withTrimmedTop (36).reduced (40));
    }

private:
    TremoloProcessor& processor;
    juce::Slider depthSlider;
};

juce::AudioProcessorEditor* TremoloProcessor::createEditor()
{
    return new SwayEditor (*this);
}
} // namespace wright

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new wright::TremoloProcessor();
}
