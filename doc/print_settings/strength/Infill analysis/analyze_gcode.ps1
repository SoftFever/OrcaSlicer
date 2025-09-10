<#
Script: G-Code Infill Layer Time Analyzer

Description: Analyzes multiple G-code files in a directory to calculate estimated printing time per layer, focusing on sparse infill movements.
Considers acceleration, deceleration, and max speed for realistic estimates.

How it works:
- Parses G-code lines to detect layers and movements.
- Tracks position and feedrate, ignoring non-sparse-infill types.
- Calculates movement times using trapezoidal/triangular motion profiles.
- Outputs a CSV with layer heights and times per file, leaving empty fields for missing data.

Usage: Run in PowerShell. Ensure .gcode files are in the directory. Outputs gcode_analysis.csv.

NOTE:
  This script is just a estimation tool and will not reflect actual printing times.
  Is just a rough estimate to Layer time Variability.
#>

param (
    [Parameter(Mandatory = $false)]
    [string]$DirectoryPath = (Get-Location).Path
)

# Get all .gcode files in the directory
$gcodeFiles = Get-ChildItem -Path $DirectoryPath -Filter "*.gcode" | Where-Object { -not $_.PSIsContainer }

if ($gcodeFiles.Count -eq 0) {
    Write-Host "No .gcode files found in $DirectoryPath." -ForegroundColor Red
    exit
}

# Initialize hashtable to store times by Z and file
$fileTimes = @{}

# Process each .gcode file
foreach ($file in $gcodeFiles) {
    $GCodeFilePath = $file.FullName
    $content = Get-Content $GCodeFilePath
    $layers = @()
    $currentLayer = $null
    $currentX = 0
    $currentY = 0
    $currentZ = 0
    $currentF = 0
    $acceleration = 500  # mm/s², assume standard value
    $deceleration = 500  # mm/s², assume standard value

    $includeMovements = $true  # Flag to include or ignore movements based on type

    foreach ($line in $content) {
        if ($line -match ";TYPE:Outer wall") {
            $includeMovements = $false
        } elseif ($line -match ";TYPE:Sparse infill") {
            $includeMovements = $true
        }

        if ($line -match ";Z:([\d.]+)") {
            $zValue = [double]$matches[1]
            if ($currentLayer) {
                $layers += $currentLayer
            }
            $currentLayer = @{
                Z = $zValue
                LayerTime = 0
            }
        } elseif ($line -match "^G1 " -and ($line -match "X|Y|Z")) {
            # Parse X, Y, Z, F
            $x = if ($line -match "X([\d.-]+)") { [double]$matches[1] } else { $currentX }
            $y = if ($line -match "Y([\d.-]+)") { [double]$matches[1] } else { $currentY }
            $z = if ($line -match "Z([\d.-]+)") { [double]$matches[1] } else { $currentZ }
            $f = if ($line -match "F([\d.-]+)") { [double]$matches[1] } else { $currentF }

            $dx = $x - $currentX
            $dy = $y - $currentY
            $dz = $z - $currentZ
            $distance = [math]::Sqrt($dx*$dx + $dy*$dy + $dz*$dz)

            if ($distance -gt 0 -and $f -gt 0 -and $currentLayer -and $includeMovements) {
                $speed = $f / 60  # mm/s
                $speed = [math]::Min($speed, 60)  # Cap at max achievable speed
                $accel_time = $speed / $acceleration
                $decel_time = $speed / $deceleration
                $accel_dist = 0.5 * $acceleration * $accel_time * $accel_time
                $decel_dist = 0.5 * $deceleration * $decel_time * $decel_time

                if ($distance -ge ($accel_dist + $decel_dist)) {
                    $const_dist = $distance - $accel_dist - $decel_dist
                    $const_time = $const_dist / $speed
                    $move_time = $accel_time + $const_time + $decel_time
                } else {
                    # Triangular movement with different acceleration and deceleration
                    $avg_accel = ($acceleration + $deceleration) / 2
                    $v_max = [math]::Sqrt($avg_accel * $distance)
                    $accel_time_tri = $v_max / $acceleration
                    $decel_time_tri = $v_max / $deceleration
                    $move_time = $accel_time_tri + $decel_time_tri
                }
                $currentLayer.LayerTime += $move_time
            }

            $currentX = $x
            $currentY = $y
            $currentZ = $z
            $currentF = $f
        }
    }
    if ($currentLayer) {
        $layers += $currentLayer
    }

    # Ignore first and last layer
    if ($layers.Count -gt 2) {
        $layers = $layers[1..($layers.Count-2)]
    }

    # Store in hashtable
    foreach ($layer in $layers) {
        $z = $layer.Z.ToString().Replace('.', ',')
        if (-not $fileTimes.ContainsKey($z)) {
            $fileTimes[$z] = @{}
        }
        if ($layer.LayerTime -gt 0) {
            $fileTimes[$z][$file.BaseName] = $layer.LayerTime.ToString("F2").Replace('.', ',')
        }
    }
}

# Fill with empty if no layer for some file at a height Z
$zKeys = $fileTimes.Keys | ForEach-Object { $_ }
foreach ($z in $zKeys) {
    # No need to fill, as we will check in output
}

# Print the result
$outputFile = Join-Path $DirectoryPath "gcode_analysis.csv"
if (Test-Path $outputFile) {
    try {
        Remove-Item $outputFile
    } catch {
        Write-Host "Cannot delete file $outputFile because it is in use. Close the file and run again." -ForegroundColor Red
        exit
    }
}
$header = "Height;" + ($gcodeFiles.BaseName -join ";")
$header | Out-File -FilePath $outputFile -Encoding UTF8

foreach ($z in ($fileTimes.Keys | Sort-Object { [double]$_.Replace(',', '.') })) {
    $times = @()
    foreach ($file in $gcodeFiles) {
        $time = $fileTimes[$z][$file.BaseName]
        if ($time) { $times += $time } else { $times += "" }
    }
    $timesStr = $times -join ";"
    "$z;$timesStr" | Out-File -FilePath $outputFile -Append -Encoding UTF8
}

Write-Host "Results saved to $outputFile"
