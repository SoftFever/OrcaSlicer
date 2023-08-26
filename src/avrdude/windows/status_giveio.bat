@set DIRVERNAME=giveio

@loaddrv status %DIRVERNAME%
@if errorlevel 1 goto error

@goto exit

:error
@echo ERROR: Status querry for %DIRVERNAME% failed

:exit

