@set DIRVERNAME=giveio

@loaddrv stop %DIRVERNAME%
@if errorlevel 2 goto error

@loaddrv remove %DIRVERNAME%
@if errorlevel 1 goto error

@goto exit

:error
@echo ERROR: Deinstallation of %DIRVERNAME% failed

:exit
