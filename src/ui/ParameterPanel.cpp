#include "ParameterPanel.h"

#include "ui/Theme.h"

namespace
{
constexpr int maximumRows = 800;
constexpr int rowHeight = 38;
}

/** One parameter: its name, a slider over the normalised range, and the value as the plugin prints it. */
class ParameterPanel::Row final : public juce::Component
{
public:
    Row (juce::AudioProcessorParameter& parameterToControl, std::function<void()> onUserChange)
        : parameter (parameterToControl), userChanged (std::move (onUserChange))
    {
        name.setText (parameter.getName (40), juce::dontSendNotification);
        name.setFont (mbs::brandFont (12.5f));
        name.setMinimumHorizontalScale (0.8f);
        addAndMakeVisible (name);

        value.setFont (mbs::monoFont (11.5f));
        value.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (value);

        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        slider.setRange (0.0, 1.0, parameter.isDiscrete() && parameter.getNumSteps() > 1 ? 1.0 / (parameter.getNumSteps() - 1) : 0.0);
        slider.setDoubleClickReturnValue (true, parameter.getDefaultValue());
        slider.setValue (parameter.getValue(), juce::dontSendNotification);
        slider.onValueChange = [this]
        {
            if (pulling)
                return;

            parameter.setValueNotifyingHost ((float) slider.getValue());
            refreshText();

            if (userChanged != nullptr)
                userChanged();
        };
        addAndMakeVisible (slider);

        restyle();
        refreshText();
    }

    void restyle()
    {
        name.setColour (juce::Label::textColourId, mbs::theme().ink);
        value.setColour (juce::Label::textColourId, mbs::theme().inkMuted);
    }

    /** Takes in a change the plugin made to itself, such as a preset or its own editor. */
    void pull()
    {
        const auto current = (double) parameter.getValue();

        if (std::abs (current - slider.getValue()) > 1.0e-4 && ! slider.isMouseButtonDown())
        {
            pulling = true;
            slider.setValue (current, juce::dontSendNotification);
            pulling = false;
            refreshText();
        }
    }

    void setDefault() { slider.setValue (parameter.getDefaultValue(), juce::sendNotificationSync); }
    void setNormalised (double normalised) { slider.setValue (normalised, juce::sendNotificationSync); }
    bool isDiscrete() const { return parameter.isDiscrete(); }
    int steps() const { return parameter.getNumSteps(); }
    juce::String getName() const { return parameter.getName (60); }

    void resized() override
    {
        auto area = getLocalBounds().reduced (4, 0);
        name.setBounds (area.removeFromLeft (juce::jmax (110, area.getWidth() * 36 / 100)));
        value.setBounds (area.removeFromRight (86));
        slider.setBounds (area.reduced (6, 0));
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (mbs::theme().line.withAlpha (0.5f));
        g.fillRect (4, getHeight() - 1, getWidth() - 8, 1);
    }

private:
    void refreshText()
    {
        auto text = parameter.getCurrentValueAsText();
        const auto label = parameter.getLabel();

        if (label.isNotEmpty() && ! text.endsWith (label))
            text += " " + label;

        value.setText (text, juce::dontSendNotification);
    }

    juce::AudioProcessorParameter& parameter;
    std::function<void()> userChanged;
    juce::Label name, value;
    juce::Slider slider;
    bool pulling = false;
};

ParameterPanel::ParameterPanel (juce::AudioPluginInstance& instance, std::function<void()> onChanged)
    : plugin (instance), changed (std::move (onChanged))
{
    search.setTextToShowWhenEmpty ("Filter parameters", mbs::theme().inkFaint);
    search.onTextChange = [this] { applyFilter(); };
    addAndMakeVisible (search);

    resetButton.onClick = [this] { resetAll(); };
    addAndMakeVisible (resetButton);

    randomButton.onClick = [this] { randomise(); };
    addAndMakeVisible (randomButton);

    countLabel.setFont (mbs::monoFont (11.0f));
    countLabel.setColour (juce::Label::textColourId, mbs::theme().inkFaint);
    addAndMakeVisible (countLabel);

    const auto& parameters = plugin.getParameters();
    truncated = parameters.size() > maximumRows;

    for (auto* parameter : parameters)
    {
        if ((int) rows.size() >= maximumRows)
            break;

        // The bypass switch is not part of the sound being measured; leave it where it is.
        if (parameter == plugin.getBypassParameter())
            continue;

        auto* row = rows.add (new Row (*parameter, [this] { if (changed != nullptr) changed(); }));
        list.addAndMakeVisible (row);
    }

    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);

    applyFilter();
    startTimerHz (8);
}

ParameterPanel::~ParameterPanel()
{
    stopTimer();
}

void ParameterPanel::timerCallback()
{
    for (auto* row : rows)
        if (row->isVisible())
            row->pull();
}

void ParameterPanel::applyFilter()
{
    const auto filter = search.getText().trim();
    int shown = 0;

    for (auto* row : rows)
    {
        const auto matches = filter.isEmpty() || row->getName().containsIgnoreCase (filter);
        row->setVisible (matches);
        shown += matches ? 1 : 0;
    }

    countLabel.setText (rows.isEmpty() ? juce::String ("This plugin has no parameters")
                        : juce::String (shown) + " of " + juce::String (rows.size()) + (truncated ? " (first " + juce::String (maximumRows) + ")" : juce::String()),
                        juce::dontSendNotification);
    layoutRows();
}

void ParameterPanel::layoutRows()
{
    int y = 0;
    const auto width = viewport.getMaximumVisibleWidth();

    for (auto* row : rows)
        if (row->isVisible())
        {
            row->setBounds (0, y, width, rowHeight);
            y += rowHeight;
        }

    list.setSize (width, juce::jmax (y, viewport.getHeight()));
}

void ParameterPanel::resetAll()
{
    for (auto* row : rows)
        row->setDefault();
}

void ParameterPanel::randomise()
{
    juce::Random random;

    for (auto* row : rows)
        row->setNormalised (row->isDiscrete() && row->steps() > 1 ? (double) random.nextInt (row->steps()) / (row->steps() - 1)
                                                                   : (double) random.nextFloat());
}

void ParameterPanel::paint (juce::Graphics& g)
{
    g.fillAll (mbs::theme().paper);
}

void ParameterPanel::resized()
{
    auto area = getLocalBounds().reduced (14, 12);

    auto top = area.removeFromTop (36);
    randomButton.setBounds (top.removeFromRight (96));
    top.removeFromRight (8);
    resetButton.setBounds (top.removeFromRight (112));
    top.removeFromRight (8);
    search.setBounds (top);

    area.removeFromTop (6);
    countLabel.setBounds (area.removeFromTop (18));
    area.removeFromTop (4);
    viewport.setBounds (area);
    layoutRows();
}
