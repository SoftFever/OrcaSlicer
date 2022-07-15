#include "GCodeChecker.h"

using namespace std;
using namespace BambuStudio;

int main(int argc, char *argv[])
{
    if (argc != 2) {
        cout << "Invalid input arguments" << endl;
        return -1;
    }
    string path(argv[1]);
    cout << "Start to check file " << path << endl;
    GCodeChecker checker;

    //BBS: parse and check whether has invalid gcode
    if (checker.parse_file(path) != GCodeCheckResult::Success) {
        cout << "Failed to parse and check file " << path << endl;
        return -1;
    }

    cout << "Success to parse and check file" << path << endl;
    return 0;
}
