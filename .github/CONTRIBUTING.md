Did you encounter an issue with using PrusaSlicer? Fear not! This guide will help you to write a good bug report in just a few, simple steps.

There is a good chance that the issue, you have encountered, is already reported. Please check the [list of reported issues](https://github.com/prusa3d/PrusaSlicer/issues) before creating a new issue report. If you find an existing issue report, feel free to add further information to that report.

If possible, please include the following information when [reporting an issue](https://github.com/prusa3d/PrusaSlicer/issues/new):
* PrusaSlicer version (See the about dialog for the version number. If running from git, please include the git commit ID from `git rev-parse HEAD` also.)
* Operating system type + version
* Steps to reproduce the issue, including:
    * Command line parameters used, if any
    * PrusaSlicer configuration file (Use ``Export Config...`` from the ``File`` menu - please don't export a bundle)
    * Expected result
    * Actual result
    * Any error messages
* If the issue is related to G-code generation, please include the following:
    * STL, OBJ or AMF input file (please make sure the input file is not broken, e.g. non-manifold, before reporting a bug)
    * a screenshot of the G-code layer with the issue (e.g. using [Pronterface](https://github.com/kliment/Printrun))

Please make sure only to include one issue per report. If you encounter multiple, unrelated issues, please report them as such.

Simon Tatham has written an excellent on article on [How to Report Bugs Effectively](http://www.chiark.greenend.org.uk/~sgtatham/bugs.html) which is well worth reading, although it is not specific to PrusaSlicer.

