@REM OrcaSlicer build script for Windows
@echo off

:: Unset errorlevel variable
:: If errorlevel is manually set, it is not updated upon program calls
set errorlevel=

:: Create a local scope for the script
setlocal enableDelayedExpansion

set WP=%CD%
set script_name=%~nx0

:: Arg definition count
set argdefn=0

:: Error check macro
set "repeat_error=if not ^!errorlevel^! == 0 exit /b ^!errorlevel^!"
set "error_check=if not ^!errorlevel^! == 0 echo Exiting script with code ^!errorlevel^! & exit /b ^!errorlevel^!"

:: Define arguments
call :add_arg build_debug bool b debug "build in Debug mode"
call :add_arg clean bool c clean "force a clean build"
call :add_arg build_deps bool d deps "download and build dependencies in ./deps/ (build prerequisite)"
call :add_arg dry_run bool D dry-run "perform a dry run of the script"
call :add_arg build_debuginfo bool e debuginfo "build in RelWithDebInfo mode"
call :add_arg print_help bool h help "print this help message"
call :add_arg pack_deps bool p pack "bundle build deps into a zip file"
call :add_arg build_slicer bool s slicer "build OrcaSlicer"
call :add_arg install_deps bool u install-deps "download and install system dependencies using WinGet (build prerequisite)"
call :add_arg use_vs2019 bool "" vs2019 "Use Visual Studio 16 2019 as the generator. Can be used with '-u'. (Default: Visual Studio 17 2022)"

:: handle arguments from input
call :handle_args %*
%error_check%

if "%debugscript%" == "ON" (
    set /A range_end = %argdefn% - 1
    for /L %%i in (0, 1, !range_end!) do (
        call :echo_var !argdefs[%%i].VARIABLE_NAME!
    )
)

if "%*" == "" (
    set print_help=ON
)

if "%print_help%" == "ON" (
    call :print_help_msg
    exit /b 0
)


if "%install_deps%" == "ON" (
    where winget >nul 2>nul
    if not !errorlevel! == 0 (
        echo WinGet was not found
        exit /b 1
    )
    set "winget_args=-e --source=winget"
    call :print_and_run winget install !winget_args! --id=Microsoft.VisualStudio.2022.BuildTools --custom "--add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 Microsoft.VisualStudio.Component.VC.CMake.Project Microsoft.VisualStudio.Component.Windows11SDK.26100"
    call :print_and_run winget install !winget_args! --id=Kitware.CMake -v "3.31.8"
    call :print_and_run winget install !winget_args! --id=StrawberryPerl.StrawberryPerl
    call :print_and_run winget install !winget_args! --id=Git.Git
    echo System dependencies have been installed. Restart the shell to reload the environment.
    exit /b 0
)

if "%build_debug%"=="ON" (
    set build_type=Debug
    set build_dir=build-dbg
) else (
    if "%build_debuginfo%"=="ON" (
        set build_type=RelWithDebInfo
        set build_dir=build-dbginfo
    ) else (
        set build_type=Release
        set build_dir=build
    )
)
echo build type set to %build_type%

set "generator=Visual Studio 17 2022"
if "%use_vs2019%" == "ON" set "generator=Visual Studio 16 2019"

set "SIG_FLAG="
if defined ORCA_UPDATER_SIG_KEY set "SIG_FLAG=-DORCA_UPDATER_SIG_KEY=%ORCA_UPDATER_SIG_KEY%"

set DEPS=%WP%\deps\%build_dir%\OrcaSlicer_dep
if "%build_deps%" == "ON" (
    echo building deps...

    if "%clean%" == "ON" (
        call :print_and_run rmdir /S /Q deps\%build_dir%
        %error_check%
    )

    call :print_and_run cmake -S deps -B deps/%build_dir% -G "%generator%" -A x64 -DDESTDIR="%DEPS%" -DCMAKE_BUILD_TYPE=%build_type% -DDEP_DEBUG=%debug% -DORCA_INCLUDE_DEBUG_INFO=%debuginfo%
    %error_check%

    call :print_and_run cmake --build deps/%build_dir% --config %build_type% --target deps -- -m
    %error_check%
)

@REM Pack deps
if "%pack_deps%" == "ON" (
    setlocal ENABLEDELAYEDEXPANSION
    cd %WP%/deps/build
    for /f "tokens=2-4 delims=/ " %%a in ('date /t') do set build_date=%%c%%b%%a
    echo packing deps: OrcaSlicer_dep_win64_!build_date!_vs2022.zip

    call :print_and_run %WP%/tools/7z.exe a OrcaSlicer_dep_win64_!build_date!_vs2022.zip OrcaSlicer_dep
    %error_check%
    endlocal
)

if "%build_slicer%" == "ON" (
    echo building Orca Slicer...

    if "%clean%" == "ON" (
        call :print_and_run rmdir /S /Q %build_dir%
        %error_check%
    )

    call :print_and_run cmake -B %build_dir% -G "%generator%" -A x64 -DBBL_RELEASE_TO_PUBLIC=1 -DORCA_TOOLS=ON %SIG_FLAG% -DCMAKE_PREFIX_PATH="%DEPS%/usr/local" -DCMAKE_INSTALL_PREFIX="./OrcaSlicer" -DCMAKE_BUILD_TYPE=%build_type% -DWIN10SDK_PATH="%WindowsSdkDir%Include\%WindowsSDKVersion%\"
    %error_check%

    call :print_and_run cmake --build %build_dir% --config %build_type% --target ALL_BUILD -- -m
    %error_check%

    call :print_and_run call scripts/run_gettext.bat

    call :print_and_run cmake --build %build_dir% --target install --config %build_type%
    %error_check%
)

:: End of script
exit /b 0



:: ---------------------------------------------
:: BEGIN FUNCTION DEFINITIONS
:: ---------------------------------------------

:debug_msg
    if "%debugscript%" == "ON" echo %*
    exit /b 0

::get_str_len <string>
:get_str_len
    setlocal
    set in=%~1
    for /L %%i in (0, 1, 100) do (
        if "!in:~%%i,1!" == "" (
            set out=%%i
            goto :break_str_len
        )
    )

    echo error in get_str_len: string is too long
    endlocal
    exit /b 1

    :break_str_len
    endlocal & set ret=%out%
    exit /b 0

:: print_help_msg
:print_help_msg
    setlocal
    ::get longest string length
    set flags=
    set max_len=0
    set /A range_end = %argdefn% - 1
    for /L %%i in (0, 1, %range_end%) do (
        set str_placeholder=
        if "!argdefs[%%i].TYPE!" == "string" (
            set "str_placeholder= <value>"
        )

        set "flag_str="
        if not "!argdefs[%%i].SHORT_FLAG!" == "" (
            set "flag_str=-!argdefs[%%i].SHORT_FLAG!!str_placeholder!"
        )

        if not "!argdefs[%%i].LONG_FLAG!" == "" (
            if "!flag_str!" == "" (
                :: add 4 spaces of padding so long flags are always aligned
                set "flag_str=    "
            ) else (
                set "flag_str=!flag_str!, "
            )
            set "flag_str=!flag_str!--!argdefs[%%i].LONG_FLAG!!str_placeholder!"
        )
        set "flag_str=!flag_str!  "
        set "flags[%%i]=!flag_str!"
        call :get_str_len "!flag_str!"
        if !ret! GTR !max_len! (
            set max_len=!ret!
        )
    )

    :: create a padding string
    set padding=
    for /L %%i in (0, 1, %max_len%) do set "padding=!padding! "

    :: begin printing help message
    echo OrcaSlicer build script for Windows
    set /A range_end = %argdefn% - 1
    for /L %%i in (0, 1, !range_end!) do (
        set "flag=!flags[%%i]!%padding%"
        echo    !flag:~0,%max_len%!!argdefs[%%i].HELP_TEXT!
    )
    echo For a first use, use './%script_name% -u'
    echo    and then './%script_name% -ds'
    endlocal
    exit /b 0

:: add_arg <variable_name> <type:bool,string> <short_flag> <long_flag> <help_text>
:add_arg
    call :debug_msg starting function: add_arg "%~1" "%~2" "%~3" "%~4" "%~5"

    set argdefs[%argdefn%].VARIABLE_NAME=%~1
    set argdefs[%argdefn%].TYPE=%~2
    set argdefs[%argdefn%].SHORT_FLAG=%~3
    set argdefs[%argdefn%].LONG_FLAG=%~4
    set argdefs[%argdefn%].HELP_TEXT=%~5

    :: ensure variable is not set
    set %~1=

    call :debug_msg VARIABLE_NAME: !argdefs[%argdefn%].VARIABLE_NAME!
    call :debug_msg TYPE: !argdefs[%argdefn%].TYPE!
    call :debug_msg SHORT_FLAG: !argdefs[%argdefn%].SHORT_FLAG!
    call :debug_msg LONG_FLAG: !argdefs[%argdefn%].LONG_FLAG!
    call :debug_msg HELP_TEXT: !argdefs[%argdefn%].HELP_TEXT!

    :: Increment argument
    set /A argdefn+=1

    exit /b 0


:: find_arg <type:short,long> <flag>
:find_arg
    setlocal
    call :debug_msg starting function: find_arg "%~1" "%~2"

    set type=
    if /I "%~1" == "short" set type=SHORT
    if /I "%~1" == "long" set type=LONG
    if not defined type (
        endlocal
        set ret=
        exit /b 1
    )

    call :debug_msg find_arg type=%type%
    set /A range_end = %argdefn% - 1
    for /L %%i in (0, 1, %range_end%) do (
        if "!argdefs[%%i].%type%_FLAG!" == "%~2" (
            set idx=%%i
            goto :find_arg_cont
        )
    )

    echo Error in find_arg: Failed to find arg "%~2"
    call :print_help_msg

    endlocal
    set ret=
    exit /b 1

    :find_arg_cont
    call :debug_msg find_arg: found at %idx%
    endlocal & (
        set ret=%idx%
    )
    exit /b 0


:: set_arg <arg_index> [<string_val>]
:set_arg
    call :debug_msg starting function: set_arg "%~1" "%~2"

    if "%~1" == "" (
        echo Error in set_arg: no index provided
        exit /b 1
    )

    setlocal
    if /I "!argdefs[%~1].TYPE!" == "bool" (
        call :debug_msg set_arg: setting bool type to ON
        set val=ON
    ) else (
        set val=%~2
        set quote_char="
        if "!val:~0,1!" == "!quote_char!" (
            if "!val:~-1,1!" == "!quote_char!" (
                set val=%val:~1,-1%
            )
        )
        call :debug_msg set_arg: setting string type to %~2
    )
    set var_name=!argdefs[%~1].VARIABLE_NAME!

    endlocal & (
        :: Set variable in parent scope
        set %var_name%=%val%
        :: Add variable to finalize command
        set "finalize_cmd=%finalize_cmd% & set "%var_name%=%val%""
    )

    exit /b 0


:: get_arg_type <arg_index>
:get_arg_type
    call :debug_msg starting function get_arg_type "%~1"
    setlocal
    set type=!argdefs[%~1].TYPE!
    endlocal & set ret=%type%
    exit /b 0


:: echo_var <variable_name>
:echo_var
    echo %~1=!%~1!
    exit /b 0


:: print_and_run <command...>
:print_and_run
    echo + %*
    if not "%dry_run%" == "ON" (
        %*
        exit /b !errorlevel!
    )
    exit /b 0

::handle_args <args...>
:handle_args
    call :debug_msg starting function handle_args "%*"
    if "%~1" == "" exit /b 0

    setlocal
    set arg=%~1
    set finalize_cmd=

    call :debug_msg Begin handling arg "%arg%"

    if not "%arg:~0,2%" == "--" goto :HAL_check_short

    ::IF long arg
    call :debug_msg Processing long arg

    call :find_arg long %arg:~2%
    %repeat_error%
    set idx=%ret%

    call :get_arg_type %idx%
    %repeat_error%
    set type=%ret%

    if /I "%type%" == "string" (
        set string_val=%2
        :: Ensure there is a string value
        if "!string_val!" == "" (
            echo Error in handle_args_loop: The option "%arg:~2%" requires a value
            exit /b 1
        )
        :: Ensure the string value isn't another argument
        if "!string_val:~0,1!" == "-" (
            echo Error in handle_args_loop: The option "%arg:~2%" requires a value. "!string_val!" is invalid as it may be another argument. Enclose the value with double quotes to override this.
            exit /b 1
        )
        shift
    )

    call :set_arg %idx% %string_val%
    %repeat_error%
    goto :handle_args_loop_reset

    :HAL_check_short
    if not "%arg:~0,1%" == "-" (
        :: IF invalid arg
        echo Unknown argument: %arg%
        call :print_help_msg
        exit /b 1
    )

    :: IF short arg
    call :debug_msg processing short args
    set /A charidx=1
    :short_arg_loop
        if "!arg:~%charidx%,1!" == "" goto :handle_args_loop_reset

        call :find_arg short !arg:~%charidx%,1!
        %repeat_error%
        set idx=%ret%

        call :get_arg_type %idx%
        %repeat_error%
        set type=%ret%

        set /A start_idx = %charidx% + 1
        if /I "%type%" == "string" (
            :: Check if there are additional characters after the found string argument
            set remaining=!arg:~%start_idx%!
            if defined remaining (
                :: If there are remaining characters, use them as the string value
                set string_val=!remaining!
            ) else (
                :: Otherwise, use the next argument
                set string_val=%2
                shift
            )
            :: Ensure there is a string value
            if "!string_val!" == "" (
                echo Error in handle_args_loop: The option "!arg:~%charidx%,1!" requires a value
                exit /b 1
            )
            :: Ensure the string value isn't another argument
            if "!string_val:~1!" == "-" (
                echo Error in handle_args_loop: The option "!arg:~%charidx%,1!" requires a value. "!string_val!" is invalid as it may be another argument. Enclose the value with double quotes to override this.
                exit /b 1
            )
            call :set_arg !idx! !string_val!
            %repeat_error%
            goto :handle_args_loop_reset
        )

        call :set_arg %idx%
        %repeat_error%
        set /A charidx+=1
    goto :short_arg_loop

    :handle_args_loop_reset
    :: End local scope for this loop and use finalize_cmd to set global variables from the set_arg function
    endlocal %finalize_cmd%
    shift
    goto :handle_args
