# Copies the files a SignPath signing request covers into the directory that request
# uploads, after checking their version information. SignPath refuses to sign a file
# whose ProductName or ProductVersion differs from its artifact configuration (see
# signpath/), and reports it only once the request has been approved, so the check runs
# before anything is submitted.
#
# -Path stages the given files at the root of -Destination.
#
# -Bundle stages a portable OrcaSlicer bundle for signpath/windows-binaries-v1.xml:
# OrcaSlicer's own binaries (-OwnBinaries) at the root, where their version information
# is checked, and every other executable, DLL and Python extension module that carries no
# signature under third-party/, at its path in the bundle. Files signed by their vendor,
# such as the Microsoft runtime, keep that signature. restore-from-signpath.ps1 copies the
# signed files back.
[CmdletBinding(DefaultParameterSetName = "Path")]
param(
    [Parameter(Mandatory = $true, ParameterSetName = "Path")]
    [string[]]$Path,

    [Parameter(Mandatory = $true, ParameterSetName = "Bundle")]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Container })]
    [string]$Bundle,

    [Parameter(ParameterSetName = "Bundle")]
    [string[]]$OwnBinaries = @("orca-slicer.exe", "OrcaSlicer.dll"),

    [Parameter(Mandatory = $true)]
    [string]$Destination,

    [Parameter(Mandatory = $true)]
    [string]$ProductVersion,

    [string]$ProductName = "OrcaSlicer"
)

$ErrorActionPreference = "Stop"

function Test-VersionInfo {
    param([string]$File)

    if (-not (Test-Path -LiteralPath $File -PathType Leaf)) {
        throw "File to sign was not found: $File"
    }

    $info = [Diagnostics.FileVersionInfo]::GetVersionInfo((Resolve-Path -LiteralPath $File).Path)
    if ($info.ProductName -ne $ProductName -or $info.ProductVersion -ne $ProductVersion) {
        throw "'$File' has ProductName '$($info.ProductName)' and ProductVersion '$($info.ProductVersion)'; SignPath expects '$ProductName' and '$ProductVersion'."
    }
}

New-Item -ItemType Directory -Force -Path $Destination | Out-Null

if ($PSCmdlet.ParameterSetName -eq "Path") {
    foreach ($file in $Path) {
        Test-VersionInfo $file
        Copy-Item -LiteralPath $file -Destination $Destination -Force
        Write-Host "Staged $(Split-Path -Leaf $file) ($ProductName $ProductVersion)"
    }
    exit 0
}

$bundleRoot = (Resolve-Path -LiteralPath $Bundle).Path.TrimEnd('\', '/')
foreach ($name in $OwnBinaries) {
    $file = Join-Path $bundleRoot $name
    Test-VersionInfo $file
    Copy-Item -LiteralPath $file -Destination $Destination -Force
    Write-Host "Staged $name ($ProductName $ProductVersion)"
}

$thirdParty = Get-ChildItem -LiteralPath $bundleRoot -Recurse -File |
    Where-Object { $_.Extension -in ".exe", ".dll", ".pyd" } |
    Where-Object { $_.DirectoryName -ne $bundleRoot -or $_.Name -notin $OwnBinaries } |
    Where-Object { (Get-AuthenticodeSignature -LiteralPath $_.FullName).Status -eq "NotSigned" }

foreach ($file in $thirdParty) {
    $relative = $file.FullName.Substring($bundleRoot.Length + 1)
    $target = Join-Path (Join-Path $Destination "third-party") $relative
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
    Copy-Item -LiteralPath $file.FullName -Destination $target -Force
}
Write-Host "Staged $(@($thirdParty).Count) unsigned third-party files under third-party/"
