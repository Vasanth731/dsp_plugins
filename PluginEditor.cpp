/*
  ==============================================================================

    VELVET CHORUS - plugin editor

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
juce::Font makePedalFont (float height, bool bold)
{
   #if defined (JUCE_MAJOR_VERSION) && JUCE_MAJOR_VERSION >= 8
    // Braces, not parentheses - the parenthesised form is parsed as a function
    // declaration rather than an object (most vexing parse).
    juce::Font f { juce::FontOptions (height) };
   #else
    juce::Font f { height };
   #endif

    return bold ? f.boldened() : f;
}

//==============================================================================
namespace
{
    const juce::Colour backdrop { 0xff0d0d0f };     // behind the enclosure
    const juce::Colour body     { 0xff262036 };         // the painted enclosure
    const juce::Colour bodyDeep { 0xff141020 };         // knob caps, recesses
    const juce::Colour accent   { 0xffb79bff };       // value arcs, labels, LED
    const juce::Colour ink      { 0xffeae6f5 };          // pointers and readouts
    const juce::Colour track    { 0xff39334a };        // unfilled part of the arc

    struct ChoiceSpec { const char* paramID; const char* caption; };
    struct KnobSpec   { const char* paramID; const char* name; const char* suffix; };

    const ChoiceSpec choiceSpecs[] = { { "type", "TYPE" } };
    const KnobSpec   knobSpecs[]   = { { "rate", "RATE", " Hz" }, { "depth", "DEPTH", " %" }, { "delay", "DELAY", " ms" }, { "feedback", "FEEDBACK", " %" }, { "width", "WIDTH", " %" }, { "tone", "TONE", " Hz" }, { "mix", "MIX", " %" }, { "level", "LEVEL", " dB" } };

    constexpr int numChoices = 1;
    constexpr int numKnobs   = 8;
    constexpr int knobCols   = 4;
    constexpr int knobRows   = 2;

    constexpr int edgeInset     = 10;   // gap between window and enclosure
    constexpr int headerHeight  = 56;
    constexpr int selectorHeight= 42;
    constexpr int footerHeight  = 84;

    /** Small caps with a little tracking - reads as panel printing rather than
        UI text, without needing a display font. */
    juce::Font panelFont (float height, bool bold, float tracking)
    {
        return makePedalFont (height, bold).withExtraKerningFactor (tracking);
    }
}

//==============================================================================
PedalLookAndFeel::PedalLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId,       ink);
    setColour (juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId,  accent.withAlpha (0.35f));

    setColour (juce::ComboBox::backgroundColourId,  bodyDeep);
    setColour (juce::ComboBox::textColourId,        accent);
    setColour (juce::ComboBox::outlineColourId,     accent.withAlpha (0.4f));
    setColour (juce::ComboBox::arrowColourId,       accent);
    setColour (juce::PopupMenu::backgroundColourId, bodyDeep);
    setColour (juce::PopupMenu::textColourId,       ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.3f));
    setColour (juce::PopupMenu::highlightedTextColourId,       juce::Colours::white);
}

void PedalLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos,
                                         float rotaryStartAngle, float rotaryEndAngle,
                                         juce::Slider& slider)
{
    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto  centre = bounds.getCentre();
    const float angle  = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const float arcRadius = juce::jmax (3.0f, radius - 2.0f);

    const float alpha = slider.isEnabled() ? 1.0f : 0.3f;

    // Unfilled track
    juce::Path trackPath;
    trackPath.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                             rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (track.withMultipliedAlpha (alpha));
    g.strokePath (trackPath, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded));

    // Value arc
    juce::Path valuePath;
    valuePath.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                             rotaryStartAngle, angle, true);
    g.setColour (accent.withMultipliedAlpha (alpha));
    g.strokePath (valuePath, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded));

    // Flat cap, no gradient - the panel reads cleaner without fake metal
    const float capRadius = juce::jmax (5.0f, arcRadius - 7.0f);
    auto cap = juce::Rectangle<float> (capRadius * 2.0f, capRadius * 2.0f).withCentre (centre);

    g.setColour (bodyDeep.withMultipliedAlpha (alpha));
    g.fillEllipse (cap);
    g.setColour (juce::Colours::black.withAlpha (0.25f * alpha));
    g.drawEllipse (cap, 1.0f);

    // Pointer runs from the centre out to the cap edge
    juce::Path pointer;
    pointer.startNewSubPath (0.0f, 0.0f);
    pointer.lineTo (0.0f, -capRadius + 2.0f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));

    g.setColour (ink.withMultipliedAlpha (alpha));
    g.strokePath (pointer, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
}

void PedalLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                         bool shouldDrawButtonAsHighlighted,
                                         bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (2.0f);
    const float d = juce::jmin (bounds.getWidth(), bounds.getHeight());
    auto circle = juce::Rectangle<float> (d, d).withCentre (bounds.getCentre());

    if (shouldDrawButtonAsDown)
        circle = circle.reduced (1.5f);

    // Outer ring
    g.setColour (bodyDeep.brighter (shouldDrawButtonAsHighlighted ? 0.35f : 0.2f));
    g.fillEllipse (circle);
    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.drawEllipse (circle, 1.5f);

    // Inner cap
    g.setColour (bodyDeep);
    g.fillEllipse (circle.reduced (d * 0.26f));
}

//==============================================================================
NewProjectAudioProcessorEditor::NewProjectAudioProcessorEditor (NewProjectAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    for (int i = 0; i < numChoices; ++i)
    {
        auto c = std::make_unique<ChoiceControl>();

        c->caption.setText (choiceSpecs[i].caption, juce::dontSendNotification);
        c->caption.setJustificationType (juce::Justification::centredLeft);
        c->caption.setColour (juce::Label::textColourId, accent.withAlpha (0.75f));
        c->caption.setFont (panelFont (10.0f, true, 0.18f));
        addAndMakeVisible (c->caption);

        c->box.setLookAndFeel (&pedalLookAndFeel);
        c->box.setJustificationType (juce::Justification::centredLeft);

        // Filled from the parameter itself, so the editor can never disagree
        // with the processor about what the options are.
        if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (
                audioProcessor.parameters.getParameter (choiceSpecs[i].paramID)))
        {
            const auto& names = param->choices;

            for (int k = 0; k < names.size(); ++k)
                c->box.addItem (names[k], k + 1);          // item IDs are 1-based
        }

        addAndMakeVisible (c->box);

        c->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            audioProcessor.parameters, choiceSpecs[i].paramID, c->box);

        choices.push_back (std::move (c));
    }

    for (int i = 0; i < numKnobs; ++i)
    {
        auto k = std::make_unique<KnobControl>();

        k->slider.setLookAndFeel (&pedalLookAndFeel);
        k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
        k->slider.setTextValueSuffix (knobSpecs[i].suffix);
        k->slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                       juce::MathConstants<float>::pi * 2.75f,
                                       true);
        addAndMakeVisible (k->slider);

        k->label.setText (knobSpecs[i].name, juce::dontSendNotification);
        k->label.setJustificationType (juce::Justification::centred);
        k->label.setColour (juce::Label::textColourId, accent.withAlpha (0.8f));
        k->label.setFont (panelFont (9.5f, true, 0.2f));
        k->label.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (k->label);

        k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            audioProcessor.parameters, knobSpecs[i].paramID, k->slider);

        knobs.push_back (std::move (k));
    }

    footswitch.setLookAndFeel (&pedalLookAndFeel);
    footswitch.setClickingTogglesState (true);
    addAndMakeVisible (footswitch);

    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        audioProcessor.parameters, "bypass", footswitch);

    // The LED is painted, not a component, so it is polled rather than pushed.
    startTimerHz (15);

    setSize (672, 466);
}

NewProjectAudioProcessorEditor::~NewProjectAudioProcessorEditor()
{
    stopTimer();

    footswitch.setLookAndFeel (nullptr);

    for (auto& c : choices)
        c->box.setLookAndFeel (nullptr);

    for (auto& k : knobs)
        k->slider.setLookAndFeel (nullptr);
}

//==============================================================================
void NewProjectAudioProcessorEditor::timerCallback()
{
    const bool bypassed = footswitch.getToggleState();

    if (bypassed != lastBypassState)
    {
        lastBypassState = bypassed;
        repaint (ledBounds.expanded (6));
    }
}

//==============================================================================
void NewProjectAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (backdrop);

    // ---- The enclosure ----
    auto shell = getLocalBounds().toFloat().reduced ((float) edgeInset);
    g.setColour (body);
    g.fillRoundedRectangle (shell, 16.0f);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawRoundedRectangle (shell.reduced (0.5f), 16.0f, 1.0f);

    // Corner screws - just enough to read as a stompbox
    const float screwInset = 16.0f;
    g.setColour (juce::Colours::black.withAlpha (0.3f));
    for (int i = 0; i < 4; ++i)
    {
        const float sx = (i % 2 == 0) ? shell.getX() + screwInset : shell.getRight() - screwInset;
        const float sy = (i < 2)      ? shell.getY() + screwInset : shell.getBottom() - screwInset;
        g.fillEllipse (sx - 3.0f, sy - 3.0f, 6.0f, 6.0f);
    }

    // ---- Name plate ----
    auto header = shell.reduced (22.0f, 0.0f).withHeight ((float) headerHeight).translated (0.0f, 8.0f);

    g.setColour (accent);
    g.setFont (panelFont (17.0f, true, 0.3f));
    g.drawFittedText ("VELVET", header.removeFromTop (24.0f).toNearestInt(),
                      juce::Justification::centredLeft, 1);

    g.setColour (ink.withAlpha (0.45f));
    g.setFont (panelFont (10.0f, false, 0.25f));
    g.drawFittedText ("CHORUS", header.removeFromTop (16.0f).toNearestInt(),
                      juce::Justification::centredLeft, 1);

    // Hairline under the name plate
    const float lineY = shell.getY() + (float) headerHeight;
    g.setColour (accent.withAlpha (0.2f));
    g.fillRect (shell.getX() + 22.0f, lineY, shell.getWidth() - 44.0f, 1.0f);

    // ---- Jack labels ----
    g.setColour (ink.withAlpha (0.3f));
    g.setFont (panelFont (9.0f, false, 0.25f));
    auto jacks = shell.reduced (22.0f, 14.0f);
    g.drawFittedText ("IN", jacks.toNearestInt().removeFromBottom (14),
                      juce::Justification::bottomLeft, 1);
    g.drawFittedText ("OUT", jacks.toNearestInt().removeFromBottom (14),
                      juce::Justification::bottomRight, 1);

    // ---- Status LED: lit when the pedal is doing something ----
    const bool bypassed = footswitch.getToggleState();
    auto led = ledBounds.toFloat();

    if (! bypassed)
    {
        g.setColour (accent.withAlpha (0.25f));
        g.fillEllipse (led.expanded (4.0f));
    }

    g.setColour (bypassed ? track : accent);
    g.fillEllipse (led);
}

void NewProjectAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (edgeInset + 12);

    area.removeFromTop (headerHeight - 4);          // name plate, drawn in paint()

    if (numChoices > 0)
    {
        auto row = area.removeFromTop (selectorHeight).reduced (0, 6);
        const int cellW = row.getWidth() / juce::jmax (1, numChoices);

        for (int i = 0; i < numChoices; ++i)
        {
            auto cell = row.removeFromLeft (cellW);
            choices[(size_t) i]->caption.setBounds (cell.removeFromLeft (56));
            choices[(size_t) i]->box.setBounds (cell.removeFromRight (
                juce::jmin (150, cell.getWidth())).reduced (2, 2));
        }
    }

    // ---- Footswitch and LED along the bottom ----
    auto footer = area.removeFromBottom (footerHeight);
    auto switchArea = footer.withSizeKeepingCentre (48, 48)
                            .withY (footer.getBottom() - 54);
    footswitch.setBounds (switchArea);

    ledBounds = juce::Rectangle<int> (10, 10).withCentre (
        { switchArea.getCentreX(), switchArea.getY() - 14 });

    // ---- Knob grid ----
    const int cellW = area.getWidth()  / knobCols;
    const int cellH = area.getHeight() / juce::jmax (1, knobRows);

    for (int i = 0; i < numKnobs; ++i)
    {
        const int row = i / knobCols;
        const int col = i % knobCols;

        juce::Rectangle<int> cell (area.getX() + col * cellW,
                                   area.getY() + row * cellH,
                                   cellW, cellH);
        cell.reduce (6, 2);

        knobs[(size_t) i]->slider.setBounds (cell.removeFromTop (cell.getHeight() - 14));
        knobs[(size_t) i]->label.setBounds (cell);
    }
}
