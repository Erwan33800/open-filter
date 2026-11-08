#include "ParameterLayout.h"
#include "params/ParamSpec.h"

namespace openfilter
{
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace params;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (const auto& spec : allParams())
    {
        const juce::ParameterID pid { spec.id, 1 };
        const juce::String name { spec.name };
        const ParamSpec* sp = &spec; // allParams() est un tableau statique : l'adresse reste valable

        switch (spec.kind)
        {
            case Kind::Float:
            {
                const double skew = spec.centre > 0.0 ? skewForCentre (spec.min, spec.max, spec.centre) : 1.0;
                juce::NormalisableRange<float> range ((float) spec.min, (float) spec.max, 0.0f, (float) skew);

                const auto attributes = juce::AudioParameterFloatAttributes()
                    .withStringFromValueFunction ([sp] (float v, int) { return juce::String (formatValue (*sp, (double) v)); })
                    .withValueFromStringFunction ([sp] (const juce::String& text)
                    {
                        double v = sp->def;
                        parseValue (*sp, text.toStdString(), v);
                        return (float) v;
                    })
                    .withAutomatable (spec.automatable);

                layout.add (std::make_unique<juce::AudioParameterFloat> (pid, name, range, (float) spec.def, attributes));
                break;
            }

            case Kind::Bool:
                layout.add (std::make_unique<juce::AudioParameterBool> (pid, name, spec.def >= 0.5,
                                                                        juce::AudioParameterBoolAttributes().withAutomatable (spec.automatable)));
                break;

            case Kind::Choice:
            {
                juce::StringArray choices;

                for (const auto& c : spec.choices)
                    choices.add (juce::String (c));

                layout.add (std::make_unique<juce::AudioParameterChoice> (pid, name, choices, (int) spec.def,
                                                                          juce::AudioParameterChoiceAttributes().withAutomatable (spec.automatable)));
                break;
            }
        }
    }

    return layout;
}
}
