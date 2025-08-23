#include <I18N.hpp>
#include <wx/string.h>
#ifndef _L
#define _L(s) Slic3r::I18N::translate(s)
#endif

namespace ProfileDescrption {
    const std::string PROFILE_DESCRIPTION_0  = _L("It has a small layer height. This results in almost negligible layer lines and high print quality. It is suitable for most printing cases.");
    const std::string PROFILE_DESCRIPTION_1  = _L("Compared with the default profile of a 0.2 mm nozzle, it has lower speeds and acceleration, and the sparse infill pattern is Gyroid. This results in much higher print quality but a much longer print time.");
    const std::string PROFILE_DESCRIPTION_2  = _L("Compared with the default profile of a 0.2 mm nozzle, it has a slightly bigger layer height. This results in almost negligible layer lines and slightly shorter print time.");
    const std::string PROFILE_DESCRIPTION_3  = _L("Compared with the default profile of a 0.2 mm nozzle, it has a bigger layer height. This results in slightly visible layer lines but shorter print time.");
    const std::string PROFILE_DESCRIPTION_4  = _L("Compared with the default profile of a 0.2 mm nozzle, it has a smaller layer height. This results in almost invisible layer lines and higher print quality but longer print time.");
    const std::string PROFILE_DESCRIPTION_5  = _L("Compared with the default profile of a 0.2 mm nozzle, it has a smaller layer lines, lower speeds and acceleration, and the sparse infill pattern is Gyroid. This results in almost invisible layer lines and much higher print quality but much longer print time.");
    const std::string PROFILE_DESCRIPTION_6  = _L("Compared with the default profile of a 0.2 mm nozzle, it has a smaller layer height. This results in minimal layer lines and higher print quality but longer print time.");
    const std::string PROFILE_DESCRIPTION_7  = _L("Compared with the default profile of a 0.2 mm nozzle, it has a smaller layer lines, lower speeds and acceleration, and the sparse infill pattern is Gyroid. This results in minimal layer lines and much higher print quality but much longer print time.");
    const std::string PROFILE_DESCRIPTION_8  = _L("It has a normal layer height. This results in average layer lines and print quality. It is suitable for most printing cases.");
    const std::string PROFILE_DESCRIPTION_9  = _L("Compared with the default profile of a 0.4 mm nozzle, it has more wall loops and a higher sparse infill density. This results in higher print strength but more filament consumption and longer print time.");
    const std::string PROFILE_DESCRIPTION_10 = _L("Compared with the default profile of a 0.4 mm nozzle, it has a bigger layer height. This results in more apparent layer lines and lower print quality, but slightly shorter print time.");
    const std::string PROFILE_DESCRIPTION_11 = _L("Compared with the default profile of a 0.4 mm nozzle, it has a bigger layer height. This results in more apparent layer lines and lower print quality, but shorter print time.");
    const std::string PROFILE_DESCRIPTION_12 = _L("Compared with the default profile of a 0.4 mm nozzle, it has a smaller layer height. This results in less apparent layer lines and higher print quality but longer print time.");
    const std::string PROFILE_DESCRIPTION_13 = _L("Compared with the default profile of a 0.4 mm nozzle, it has a smaller layer height, lower speeds and acceleration, and the sparse infill pattern is Gyroid. This results in less apparent layer lines and much higher print quality but much longer print time.");
    const std::string PROFILE_DESCRIPTION_14 = _L("Compared with the default profile of a 0.4 mm nozzle, it has a smaller layer height. This results in almost negligible layer lines and higher print quality but longer print time.");
    const std::string PROFILE_DESCRIPTION_15 = _L("Compared with the default profile of a 0.4 mm nozzle, it has a smaller layer height, lower speeds and acceleration, and the sparse infill pattern is Gyroid. This results in almost negligible layer lines and much higher print quality but much longer print time.");
    const std::string PROFILE_DESCRIPTION_16 = _L("Compared with the default profile of a 0.4 mm nozzle, it has a smaller layer height. This results in almost negligible layer lines and longer print time.");
    const std::string PROFILE_DESCRIPTION_17 = _L("It has a big layer height. This results in apparent layer lines and ordinary print quality and print time.");
    const std::string PROFILE_DESCRIPTION_18 = _L("Compared with the default profile of a 0.6 mm nozzle, it has more wall loops and a higher sparse infill density. This results in higher print strength but more filament consumption and longer print time.");
    const std::string PROFILE_DESCRIPTION_19 = _L("Compared with the default profile of a 0.6 mm nozzle, it has a bigger layer height. This results in more apparent layer lines and lower print quality, but shorter print time in some cases.");
    const std::string PROFILE_DESCRIPTION_20 = _L("Compared with the default profile of a 0.6 mm nozzle, it has a bigger layer height. This results in much more apparent layer lines and much lower print quality, but shorter print time in some cases.");
    const std::string PROFILE_DESCRIPTION_21 = _L("Compared with the default profile of a 0.6 mm nozzle, it has a smaller layer height. This results in less apparent layer lines and slight higher print quality but longer print time.");
    const std::string PROFILE_DESCRIPTION_22 = _L("Compared with the default profile of a 0.6 mm nozzle, it has a smaller layer height. This results in less apparent layer lines and higher print quality but longer print time.");
    const std::string PROFILE_DESCRIPTION_23 = _L("It has a very big layer height. This results in very apparent layer lines, low print quality and shorter print time.");
    const std::string PROFILE_DESCRIPTION_24 = _L("Compared with the default profile of a 0.8 mm nozzle, it has a bigger layer height. This results in very apparent layer lines and much lower print quality, but shorter print time in some cases.");
    const std::string PROFILE_DESCRIPTION_25 = _L("Compared with the default profile of a 0.8 mm nozzle, it has a much bigger layer height. This results in extremely apparent layer lines and much lower print quality, but much shorter print time in some cases.");
    const std::string PROFILE_DESCRIPTION_26 = _L("Compared with the default profile of a 0.8 mm nozzle, it has a slightly smaller layer height. This results in slightly less but still apparent layer lines and slightly higher print quality but longer print time in some cases.");
    const std::string PROFILE_DESCRIPTION_27 = _L("Compared with the default profile of a 0.8 mm nozzle, it has a smaller layer height. This results in less but still apparent layer lines and slightly higher print quality but longer print time in some cases.");
}