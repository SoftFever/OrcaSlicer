#!powershell
#
# This script downloads, configures and builds Slic3r PE dependencies for Unix.
# (That is, all dependencies except perl + wxWidgets.)
#
# To use this script, launch the Visual Studio command line,
# `cd` into the directory containing this script and use this command:
#
#     powershell .\slic3r-makedeps.ps1
#
# The dependencies will be downloaded and unpacked into the current dir.
# This script WILL NOT try to guess the build architecture (64 vs 32 bits),
# it will by default build the 64-bit variant. To build the 32-bit variant, use:
#
#     powershell .\slic3r-makedeps.ps1 -b32
#
# Built libraries are installed into $destdir,
# which by default is C:\local\slic3r-destdir-$bits
# You can customize the $destdir using:
#
#     powershell .\slic3r-makedeps.ps1 -destdir C:\foo\bar
#
# To pass the $destdir path along to cmake, set the use CMAKE_PREFIX_PATH variable
# and set it to $destdir\usr\local
#
# Script requirements: PowerShell 3.0, .NET 4.5
#


param(
    [switch]$b32 = $false,
    [string]$destdir = ""
)

if ($destdir -eq "") {
    $destdir = "C:\local\slic3r-destdir-" + ('32', '64')[!$b32]
}

$BOOST = 'boost_1_63_0'
$CURL = 'curl-7.58.0'
$TBB_SHA = 'a0dc9bf76d0120f917b641ed095360448cabc85b'
$TBB = "tbb-$TBB_SHA"


try
{


# Set up various settings and utilities:
[Environment]::CurrentDirectory = Get-Location
$NPROC = (Get-WmiObject -class Win32_processor).NumberOfLogicalProcessors
Add-Type -A System.IO.Compression.FileSystem
#   This fxies SSL/TLS errors, credit goes to Ansible; see their `win_get_url.ps1` file
$security_protcols = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::SystemDefault
if ([Net.SecurityProtocolType].GetMember('Tls11').Count -gt 0) {
    $security_protcols = $security_protcols -bor [Net.SecurityProtocolType]::Tls11
}
if ([Net.SecurityProtocolType].GetMember('Tls12').Count -gt 0) {
    $security_protcols = $security_protcols -bor [Net.SecurityProtocolType]::Tls12
}
[Net.ServicePointManager]::SecurityProtocol = $security_protcols
$webclient = New-Object System.Net.WebClient


# Ensure DESTDIR exists:
mkdir $destdir -ea 0
mkdir "$destdir\usr\local" -ea 0


# Download sources:
echo 'Downloading sources ...'
if (!(Test-Path "$BOOST.zip")) { $webclient.DownloadFile("https://dl.bintray.com/boostorg/release/1.63.0/source/$BOOST.zip", "$BOOST.zip") }
if (!(Test-Path "$TBB.zip")) { $webclient.DownloadFile("https://github.com/wjakob/tbb/archive/$TBB_SHA.zip", "$TBB.zip") }
if (!(Test-Path "$CURL.zip")) { $webclient.DownloadFile("https://curl.haxx.se/download/$CURL.zip", ".\$CURL.zip") }


# Unpack sources:
echo 'Unpacking ...'
if (!(Test-Path $BOOST)) { [IO.Compression.ZipFile]::ExtractToDirectory("$BOOST.zip", '.') }
if (!(Test-Path $TBB)) { [IO.Compression.ZipFile]::ExtractToDirectory("$TBB.zip", '.') }
if (!(Test-Path $CURL)) { [IO.Compression.ZipFile]::ExtractToDirectory("$CURL.zip", '.') }


# Build libraries:
echo 'Building ...'

# Build boost
pushd "$BOOST"
.\bootstrap
$adr_mode = ('32', '64')[!$b32]
.\b2 `
    -j "$NPROC" `
    --with-system `
    --with-filesystem `
    --with-thread `
    --with-log `
    --with-locale `
    --with-regex `
    "--prefix=$destdir/usr/local" `
    "address-model=$adr_mode" `
    toolset=msvc-12.0 `
    link=static `
    variant=release `
    threading=multi `
    boost.locale.icu=off `
    install
popd

# Build TBB
pushd "$TBB"
mkdir 'mybuild' -ea 0
cd 'mybuild'
$generator = ('Visual Studio 12', 'Visual Studio 12 Win64')[!$b32]
cmake .. `
    -G "$generator" `
    -DCMAKE_CONFIGURATION_TYPES=Release `
    -DTBB_BUILD_SHARED=OFF `
    -DTBB_BUILD_TESTS=OFF "-DCMAKE_INSTALL_PREFIX:PATH=$destdir\usr\local"
msbuild /P:Configuration=Release INSTALL.vcxproj
popd

# Build libcurl:
pushd "$CURL\winbuild"
$machine = ("x86", "x64")[!$b32]
nmake /f Makefile.vc mode=static VC=12 GEN_PDB=yes DEBUG=no "MACHINE=$machine"
Copy-Item -R -Force ..\builds\libcurl-*-winssl\include\* "$destdir\usr\local\include\"
Copy-Item -R -Force ..\builds\libcurl-*-winssl\lib\* "$destdir\usr\local\lib\"
popd


echo ""
echo "All done!"
echo ""


}
catch [Exception]
{
    # This prints errors in a verbose manner
    echo $_.Exception|format-list -force
}
