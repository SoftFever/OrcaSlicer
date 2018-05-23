@set DIRVERNAME=giveio
@set DIRVERFILE=%DIRVERNAME%.sys

@echo Copying the driver to the windows directory
@echo target file: %WINDIR%\%DIRVERFILE%
@copy %DIRVERFILE% %WINDIR%\%DIRVERFILE%

@echo Remove a running service if needed...
@loaddrv stop %DIRVERNAME% >NUL
@if errorlevel 2 goto install

@loaddrv remove %DIRVERNAME% >NUL
@if errorlevel 1 goto install

:install
@echo Installing Windows NT/2k/XP driver: %DIRVERNAME%

@loaddrv install %DIRVERNAME% %WINDIR%\%DIRVERFILE%
@if errorlevel 3 goto error

@loaddrv start %DIRVERNAME%
@if errorlevel 1 goto error

@loaddrv starttype %DIRVERNAME% auto
@if errorlevel 1 goto error

@echo Success
@goto exit

:error
@echo ERROR: Installation of %DIRVERNAME% failed

:exit

