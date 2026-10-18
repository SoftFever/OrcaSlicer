# Copies the files SignPath signed back over the originals in the portable bundle they
# were staged from with stage-for-signpath.ps1 -Bundle: the root of the signed artifact
# holds OrcaSlicer's own binaries, third-party/ the other files under their paths in the
# bundle.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Container })]
    [string]$SignedDirectory,

    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Container })]
    [string]$Bundle
)

$ErrorActionPreference = "Stop"

$signedRoot = (Resolve-Path -LiteralPath $SignedDirectory).Path.TrimEnd('\', '/')
$thirdPartyPrefix = "third-party" + [IO.Path]::DirectorySeparatorChar
$files = Get-ChildItem -LiteralPath $signedRoot -Recurse -File
if (-not $files) {
    throw "No signed files found in '$signedRoot'."
}

foreach ($file in $files) {
    $relative = $file.FullName.Substring($signedRoot.Length + 1)
    if ($relative.StartsWith($thirdPartyPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        $relative = $relative.Substring($thirdPartyPrefix.Length)
    }

    # Every signed file replaces one that was staged, so a file without an original means
    # the artifact does not have the layout stage-for-signpath.ps1 produces.
    $target = Join-Path $Bundle $relative
    if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
        throw "Signed file '$relative' has no original in '$Bundle'."
    }
    Copy-Item -LiteralPath $file.FullName -Destination $target -Force
}

Write-Host "Restored $($files.Count) signed files into '$Bundle'."
