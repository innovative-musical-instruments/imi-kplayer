#include <juce_core/juce_core.h>

int main()
{
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);
    runner.runAllTests();

    int numFailures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        numFailures += runner.getResult(i)->failures;

    return numFailures == 0 ? 0 : 1;
}
