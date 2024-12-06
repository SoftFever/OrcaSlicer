# Microsoft Developer Studio Project File - Name="libnoise" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 5.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Dynamic-Link Library" 0x0102

CFG=libnoise - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "libnoise.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "libnoise.mak" CFG="libnoise - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "libnoise - Win32 Release" (based on\
 "Win32 (x86) Dynamic-Link Library")
!MESSAGE "libnoise - Win32 Debug" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE 

# Begin Project
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "libnoise - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "win32/Release"
# PROP Intermediate_Dir "win32/Release"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MT /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /YX /FD /c
# ADD CPP /nologo /MT /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /YX /FD /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /o NUL /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /o NUL /win32
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:windows /dll /machine:I386
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:windows /dll /machine:I386
# SUBTRACT LINK32 /map

!ELSEIF  "$(CFG)" == "libnoise - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "win32/Debug"
# PROP Intermediate_Dir "win32/Debug"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MTd /W3 /Gm /GX /Zi /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /YX /FD /c
# ADD CPP /nologo /MTd /W3 /Gm /GX /Zi /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /YX /FD /c
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /o NUL /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /o NUL /win32
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:windows /dll /debug /machine:I386 /pdbtype:sept
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:windows /dll /map /debug /machine:I386 /pdbtype:sept

!ENDIF 

# Begin Target

# Name "libnoise - Win32 Release"
# Name "libnoise - Win32 Debug"
# Begin Group "modules"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\src\module\abs.cpp
# End Source File
# Begin Source File

SOURCE=.\src\module\abs.h
# End Source File
# Begin Source File

SOURCE=.\src\module\add.cpp
# End Source File
# Begin Source File

SOURCE=.\src\module\add.h
# End Source File
# Begin Source File

SOURCE=.\src\module\billow.cpp
# End Source File
# Begin Source File

SOURCE=.\src\module\billow.h
# End Source File
# Begin Source File

SOURCE=.\src\module\blend.cpp
# End Source File
# Begin Source File

SOURCE=.\src\module\blend.h
# End Source File
# Begin Source File

SOURCE=.\src\module\cache.cpp
# End Source File
# Begin Source File

SOURCE=.\src\module\cache.h
# End Source File
# Begin Source File

SOURCE=.\src\module\checkerboard.cpp
# End Source File
# Begin Source File

SOURCE=.\src\module\checkerboard.h
# End Source File
# Begin Source File

SOURCE=.\src\module\clamp.cpp
# End Source File
# Begin Source File

SOURCE=.\src\module\clamp.h
# End Source File
# Begin Source File

SOURCE=.\src\module\const.cpp
# End Source File
# Begin Source File

SOURCE=.\src\module\const.h
# End Source File
# Begin Source File

SOURCE=.\src\module\curve.cpp
# End Source File
# Begin Source File

SOURCE=.\src\module\curve.h
# End Source File
# Begin Source File

SOURCE=.\src\module\cylinders.cpp
# End Source File
# Begin Source File

SOURCE=.\src\module\cylinders.h
# End Source File
# Begin Source File

SOURCE=.\src\module\displace.cpp
# End Source File
# Begin Source File

SOURCE=.\src\module\displace.h
# End Source File
# Begin Source File

SOURCE=.\src\module\exponent.cpp
# End Source File
# Begin Source File

SOURCE=.\src\module\exponent.h
# End Source File
# Begin Source File

SOURCE=.\src\module\invert.cpp
# End Source File
# Begin Source File

SOURCE=.\src\module\invert.h
# End Source File
# Begin Source File

SOURCE=.\src\module\max.cpp
# End Source File
# Begin Source File

SOURCE=.\src\module\max.h
# End Source File
# Begin Source File

SOURCE=.\src\module\min.cpp
# End Source File
# Begin Source File

SOURCE=.\src\module\min.h
# End Source File
# Begin Source File

SOURCE=.\src\module\module.h
# End Source File
# Begin Source File

SOURCE=.\src\module\modulebase.cpp
# End Source File
# Begin Source File

SOURCE=.\src\module\modulebase.h
# End Source File
# Begin Source File

SOURCE=.\src\module\multiply.cpp
# End Source File
# Begin Source File

SOURCE=.\src\module\multiply.h
# End Source File
# Begin Source File

SOURCE=.\src\module\perlin.cpp
# End Source File
# Begin Source File

SOURCE=.\src\module\perlin.h
# End Source File
# Begin Source File

SOURCE=.\src\module\power.cpp
# End Source File
# Begin Source File

SOURCE=.\src\module\power.h
# End Source File
# Begin Source File

SOURCE=.\src\module\ridgedmulti.cpp
# End Source File
# Begin Source File

SOURCE=.\src\module\ridgedmulti.h
# End Source File
# Begin Source File

SOURCE=.\src\module\rotatepoint.cpp
# End Source File
# Begin Source File

SOURCE=.\src\module\rotatepoint.h
# End Source File
# Begin Source File

SOURCE=.\src\module\scalebias.cpp
# End Source File
# Begin Source File

SOURCE=.\src\module\scalebias.h
# End Source File
# Begin Source File

SOURCE=.\src\module\scalepoint.cpp
# End Source File
# Begin Source File

SOURCE=.\src\module\scalepoint.h
# End Source File
# Begin Source File

SOURCE=.\src\module\select.cpp
# End Source File
# Begin Source File

SOURCE=.\src\module\select.h
# End Source File
# Begin Source File

SOURCE=.\src\module\spheres.cpp
# End Source File
# Begin Source File

SOURCE=.\src\module\spheres.h
# End Source File
# Begin Source File

SOURCE=.\src\module\terrace.cpp
# End Source File
# Begin Source File

SOURCE=.\src\module\terrace.h
# End Source File
# Begin Source File

SOURCE=.\src\module\translatepoint.cpp
# End Source File
# Begin Source File

SOURCE=.\src\module\translatepoint.h
# End Source File
# Begin Source File

SOURCE=.\src\module\turbulence.cpp
# End Source File
# Begin Source File

SOURCE=.\src\module\turbulence.h
# End Source File
# Begin Source File

SOURCE=.\src\module\voronoi.cpp
# End Source File
# Begin Source File

SOURCE=.\src\module\voronoi.h
# End Source File
# End Group
# Begin Group "models"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\src\model\cylinder.cpp
# End Source File
# Begin Source File

SOURCE=.\src\model\cylinder.h
# End Source File
# Begin Source File

SOURCE=.\src\model\line.cpp
# End Source File
# Begin Source File

SOURCE=.\src\model\line.h
# End Source File
# Begin Source File

SOURCE=.\src\model\model.h
# End Source File
# Begin Source File

SOURCE=.\src\model\plane.cpp
# End Source File
# Begin Source File

SOURCE=.\src\model\plane.h
# End Source File
# Begin Source File

SOURCE=.\src\model\sphere.cpp
# End Source File
# Begin Source File

SOURCE=.\src\model\sphere.h
# End Source File
# End Group
# Begin Group "win32"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\src\win32\dllmain.cpp
# End Source File
# Begin Source File

SOURCE=.\src\win32\libnoise.def
# End Source File
# Begin Source File

SOURCE=.\src\win32\noise.rc
# End Source File
# End Group
# Begin Source File

SOURCE=.\src\basictypes.h
# End Source File
# Begin Source File

SOURCE=.\src\exception.h
# End Source File
# Begin Source File

SOURCE=.\src\interp.h
# End Source File
# Begin Source File

SOURCE=.\src\latlon.cpp
# End Source File
# Begin Source File

SOURCE=.\src\latlon.h
# End Source File
# Begin Source File

SOURCE=.\src\mathconsts.h
# End Source File
# Begin Source File

SOURCE=.\src\misc.h
# End Source File
# Begin Source File

SOURCE=.\src\noise.h
# End Source File
# Begin Source File

SOURCE=.\src\noisegen.cpp
# End Source File
# Begin Source File

SOURCE=.\src\noisegen.h
# End Source File
# Begin Source File

SOURCE=.\src\vectortable.h
# End Source File
# End Target
# End Project
