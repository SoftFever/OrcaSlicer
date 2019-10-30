#ifndef CATCH_MAIN
#define CATCH_MAIN

#define CATCH_CONFIG_EXTERNAL_INTERFACES
#define CATCH_CONFIG_MAIN
#define CATCH_CONFIG_DEFAULT_REPORTER "verboseconsole"
#include <catch2/catch.hpp>

namespace Catch {
struct VerboseConsoleReporter : public ConsoleReporter {
    double duration = 0.;
    using ConsoleReporter::ConsoleReporter;
    
    void testCaseStarting(TestCaseInfo const& _testInfo) override
    {
        Colour::use(Colour::Cyan);
        stream << "Testing ";
        Colour::use(Colour::None);
        stream << _testInfo.name << std::endl;
        ConsoleReporter::testCaseStarting(_testInfo);
    }
    
    void sectionStarting(const SectionInfo &_sectionInfo) override
    {
        if (_sectionInfo.name != currentTestCaseInfo->name)
            stream << _sectionInfo.name << std::endl;
        
        ConsoleReporter::sectionStarting(_sectionInfo);
    }
    
    void sectionEnded(const SectionStats &_sectionStats) override {
        duration += _sectionStats.durationInSeconds;
        ConsoleReporter::sectionEnded(_sectionStats);
    } 
    
    void testCaseEnded(TestCaseStats const& stats) override
    {
        if (stats.totals.assertions.allOk()) {
            Colour::use(Colour::BrightGreen);
            stream << "Passed";
            Colour::use(Colour::None);
            stream << " in " << duration << " [seconds]\n" << std::endl;
        }
        
        duration = 0.;            
        ConsoleReporter::testCaseEnded(stats);
    }
};

CATCH_REGISTER_REPORTER( "verboseconsole", VerboseConsoleReporter )

} // namespace Catch

#endif // CATCH_MAIN
