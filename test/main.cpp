/*
  ==============================================================================

    Runs 3DQ's unit tests.

        3dq_tests                  every test
        3dq_tests <category>       one category: dsp, model, plugin
        3dq_tests --list           the categories

    Exits with 1 if anything failed.

  ==============================================================================
*/

#include <JuceHeader.h>
#include <iostream>

namespace
{
class Runner final : public juce::UnitTestRunner
{
    void logMessage (const juce::String& message) override
    {
        std::cout << message << std::endl;
    }
};
}

int main (int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI juce;
    const juce::String arg = argc > 1 ? argv[1] : "";

    if (arg == "--list")
    {
        for (const auto& category : juce::UnitTest::getAllCategories())
            std::cout << category << std::endl;
        return 0;
    }

    Runner runner;
    runner.setAssertOnFailure (false);

    if (arg.isEmpty())
        runner.runAllTests();
    else
        runner.runTestsInCategory (arg);

    int passes = 0, failures = 0;

    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        passes += runner.getResult (i)->passes;
        failures += runner.getResult (i)->failures;
    }

    if (runner.getNumResults() == 0)
    {
        std::cout << "No tests in category \"" << arg << "\"" << std::endl;
        return 1;
    }

    std::cout << std::endl << (failures == 0 ? "ALL PASSED" : "FAILED") << ": " << passes << " checks passed, "
              << failures << " failed" << std::endl;
    return failures == 0 ? 0 : 1;
}
