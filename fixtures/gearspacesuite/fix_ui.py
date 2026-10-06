import os, re

root_dir = "/Users/williamwright/dev stuff/GearspaceSuite"
src_dir = os.path.join(root_dir, "GainMatchedSat/Source")

# 1. Update CMakeLists to change Product Name
cmake_path = os.path.join(root_dir, "GainMatchedSat/CMakeLists.txt")
with open(cmake_path, "r") as f:
    cmake = f.read()

cmake = re.sub(r'PRODUCT_NAME "GainMatchedSat"', 'PRODUCT_NAME "RetroSauce"', cmake)
with open(cmake_path, "w") as f:
    f.write(cmake)

# 2. Add visual feedback in paint() in PluginEditor.cpp
ed_cpp_path = os.path.join(src_dir, "PluginEditor.cpp")
with open(ed_cpp_path, "r") as f:
    ed_cpp = f.read()

# I need to add state fetching in paint() or just bind the button and slider to trigger repaint.
# A slider moving automatically triggers repaint of the slider, but we are drawing in the main Editor's paint().
# Better: make the slider's LookAndFeel draw the indicator!

# Let's write a custom LookAndFeel class in PluginEditor.h
ed_h_path = os.path.join(src_dir, "PluginEditor.h")
with open(ed_h_path, "r") as f:
    ed_h = f.read()

custom_laf = """
class RetroLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                          const float rotaryStartAngle, const float rotaryEndAngle, juce::Slider& slider) override
    {
        // For the main knobs, we draw nothing (they are invisible over the image)
        // Wait, the styleSlider is ALSO a rotary slider. How do we distinguish?
        // We can check slider.getName()
        if (slider.getName() == "STYLE") {
            // Draw a red glowing dot indicating the position
            float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
            float radius = width * 0.4f;
            float cx = x + width * 0.5f;
            float cy = y + height * 0.5f;
            float dotX = cx + radius * std::cos(angle - juce::MathConstants<float>::halfPi);
            float dotY = cy + radius * std::sin(angle - juce::MathConstants<float>::halfPi);
            
            g.setColour(juce::Colours::red);
            g.fillEllipse(dotX - 5, dotY - 5, 10, 10);
            
            // Draw a subtle glow
            g.setColour(juce::Colours::red.withAlpha(0.3f));
            g.fillEllipse(dotX - 10, dotY - 10, 20, 20);
        } else {
            // Main knobs: Draw an indicator line
            float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
            float radius = width * 0.35f;
            float cx = x + width * 0.5f;
            float cy = y + height * 0.5f;
            float dotX = cx + radius * std::cos(angle - juce::MathConstants<float>::halfPi);
            float dotY = cy + radius * std::sin(angle - juce::MathConstants<float>::halfPi);
            
            g.setColour(juce::Colours::orange);
            g.drawLine(cx, cy, dotX, dotY, 4.0f);
        }
    }
    
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        if (button.getName() == "POWER") {
            // Draw a LED
            float cx = button.getWidth() * 0.5f;
            float cy = button.getHeight() * 0.5f - 40; // Draw it above the switch
            if (button.getToggleState()) {
                g.setColour(juce::Colours::limegreen);
                g.fillEllipse(cx - 8, cy - 8, 16, 16);
                g.setColour(juce::Colours::limegreen.withAlpha(0.4f));
                g.fillEllipse(cx - 15, cy - 15, 30, 30);
            } else {
                g.setColour(juce::Colours::red);
                g.fillEllipse(cx - 8, cy - 8, 16, 16);
                g.setColour(juce::Colours::red.withAlpha(0.4f));
                g.fillEllipse(cx - 15, cy - 15, 30, 30);
            }
        }
    }
};
"""

# Insert custom LAF
ed_h = re.sub(r'class GainMatchedSatAudioProcessorEditor', custom_laf + '\nclass GainMatchedSatAudioProcessorEditor', ed_h)

# Add member var to Editor
ed_h = re.sub(r'juce::Image backgroundImage;', 'juce::Image backgroundImage;\n    RetroLookAndFeel customLaf;', ed_h)

with open(ed_h_path, "w") as f:
    f.write(ed_h)

# 3. Update Editor.cpp to set LookAndFeel and Names
ed_cpp = re.sub(r'addAndMakeVisible\(driveSlider\);', 'driveSlider.setName("DRIVE"); driveSlider.setLookAndFeel(&customLaf); addAndMakeVisible(driveSlider);', ed_cpp)
ed_cpp = re.sub(r'addAndMakeVisible\(mixSlider\);', 'mixSlider.setName("MIX"); mixSlider.setLookAndFeel(&customLaf); addAndMakeVisible(mixSlider);', ed_cpp)
ed_cpp = re.sub(r'addAndMakeVisible\(outputSlider\);', 'outputSlider.setName("OUTPUT"); outputSlider.setLookAndFeel(&customLaf); addAndMakeVisible(outputSlider);', ed_cpp)

ed_cpp = re.sub(r'powerButton\.setClickingTogglesState\(true\);', 'powerButton.setClickingTogglesState(true); powerButton.setName("POWER"); powerButton.setLookAndFeel(&customLaf);', ed_cpp)
ed_cpp = re.sub(r'styleSlider\.setSliderStyle\(juce::Slider::RotaryHorizontalVerticalDrag\);', 'styleSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag); styleSlider.setName("STYLE"); styleSlider.setLookAndFeel(&customLaf);', ed_cpp)

# Fix destructor to clear LookAndFeel
destructor = """
GainMatchedSatAudioProcessorEditor::~GainMatchedSatAudioProcessorEditor()
{
    driveSlider.setLookAndFeel(nullptr);
    mixSlider.setLookAndFeel(nullptr);
    outputSlider.setLookAndFeel(nullptr);
    powerButton.setLookAndFeel(nullptr);
    styleSlider.setLookAndFeel(nullptr);
}
"""
ed_cpp = re.sub(r'GainMatchedSatAudioProcessorEditor::~GainMatchedSatAudioProcessorEditor\(\)\s*\{[\s\S]*?\}', destructor, ed_cpp)

with open(ed_cpp_path, "w") as f:
    f.write(ed_cpp)

