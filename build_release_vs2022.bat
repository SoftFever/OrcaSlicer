@REM OrcaSlicer build script for Windows
@echo off

:: Unset errorlevel variable
:: If errorlevel is manually set, it is not updated upon program calls
set errorlevel=

:: Create a local scope for the script
setlocal enableDelayedExpansion

set WP=%CD%

:: Arg definition count
set argdefn=0

:: Error check macro
set "error_check=if not ^!errorlevel^! == 0 echo Exiting script with code ^!errorlevel^! & exit /b ^!errorlevel^!"

:: Define arguments
call :add_arg pack_deps bool p pack
call :add_arg build_deps bool d deps
call :add_arg build_slicer bool s slicer
call :add_arg build_debug bool b debug
call :add_arg build_debuginfo bool e debuginfo

:: handle arguments from input
:handle_args_loop
    if "%1" == "" goto :handle_args_loop_end

    setlocal
    set arg=%1
    set finalize_cmd=

    call :debug_msg Begin handling arg "%arg%"

    if not "%arg:~0,2%" == "--" goto :HAL_check_short
    
    ::IF long arg
    call :debug_msg Processing long arg
    
    call :find_arg long %arg:~2%
    %error_check%
    set idx=%ret%
    
    call :get_arg_type %idx%
    %error_check%
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
    %error_check%
    goto :handle_args_loop_reset

    :HAL_check_short
    if not "%arg:~0,1%" == "-" (
        :: IF invalid arg
        echo Unknown argument: %arg%
        exit /b 1
    )

    :: IF short arg
    call :debug_msg processing short args
    set /A charidx=1
    :short_arg_loop
        if "!arg:~%charidx%,1!" == "" goto :handle_args_loop_reset
        
        call :find_arg short !arg:~%charidx%,1!
        %error_check%
        set idx=%ret%
        
        call :get_arg_type %idx%
        %error_check%
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
            %error_check%
            goto :handle_args_loop_reset
        )
        
        call :set_arg %idx%
        %error_check%
        set /A charidx+=1
    goto :short_arg_loop

    :handle_args_loop_reset
    :: End local scope for this loop and use finalize_cmd to set global variables from the set_arg function
    endlocal %finalize_cmd%
    shift
    goto :handle_args_loop

:handle_args_loop_end


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

set "SIG_FLAG="
if defined ORCA_UPDATER_SIG_KEY set "SIG_FLAG=-DORCA_UPDATER_SIG_KEY=%ORCA_UPDATER_SIG_KEY%"

set DEPS=%WP%\deps\%build_dir%\OrcaSlicer_dep
if "%build_deps%" == "ON" (
    echo building deps...

    cmake -S deps -B deps/%build_dir% -G "Visual Studio 17 2022" -A x64 -DDESTDIR="%DEPS%" -DCMAKE_BUILD_TYPE=%build_type% -DDEP_DEBUG=%debug% -DORCA_INCLUDE_DEBUG_INFO=%debuginfo%
    %error_check%

    cmake --build deps/%build_dir% --config %build_type% --target deps -- -m
    %error_check%
)

@REM Pack deps
if "%pack_deps%" == "ON" (
    setlocal ENABLEDELAYEDEXPANSION
    cd %WP%/deps/build
    for /f "tokens=2-4 delims=/ " %%a in ('date /t') do set build_date=%%c%%b%%a
    echo packing deps: OrcaSlicer_dep_win64_!build_date!_vs2022.zip

    %WP%/tools/7z.exe a OrcaSlicer_dep_win64_!build_date!_vs2022.zip OrcaSlicer_dep
    %error_check%
    endlocal
)

if "%build_slicer%" == "ON" (
    echo building Orca Slicer...

    cmake -B %build_dir% -G "Visual Studio 17 2022" -A x64 -DBBL_RELEASE_TO_PUBLIC=1 -DORCA_TOOLS=ON %SIG_FLAG% -DCMAKE_PREFIX_PATH="%DEPS%/usr/local" -DCMAKE_INSTALL_PREFIX="./OrcaSlicer" -DCMAKE_BUILD_TYPE=%build_type% -DWIN10SDK_PATH="%WindowsSdkDir%Include\%WindowsSDKVersion%\"
    %error_check%

    cmake --build %build_dir% --config %build_type% --target ALL_BUILD -- -m
    %error_check%

    call scripts/run_gettext.bat

    cmake --build %build_dir% --target install --config %build_type%
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

:: add_arg <variable_name> <type:bool,string> <short_flag> [<long_flag>]
:add_arg
    call :debug_msg starting function: add_arg "%~1" "%~2" "%~3" "%~4"

    set argdefs[%argdefn%].VARIABLE_NAME=%~1
    set argdefs[%argdefn%].TYPE=%~2
    set argdefs[%argdefn%].SHORT_FLAG=%~3
    set argdefs[%argdefn%].LONG_FLAG=%~4

    :: ensure variable is not set
    set %~1=

    call :debug_msg VARIABLE_NAME: !argdefs[%argdefn%].VARIABLE_NAME!
    call :debug_msg TYPE: !argdefs[%argdefn%].TYPE!
    call :debug_msg SHORT_FLAG: !argdefs[%argdefn%].SHORT_FLAG!
    call :debug_msg LONG_FLAG: !argdefs[%argdefn%].LONG_FLAG!

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
