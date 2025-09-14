# Script to run klipper_estimator.exe on all .gcode files in the folder and save layer_times to CSV

# Set Spanish culture for number formatting and CSV delimiter
[System.Threading.Thread]::CurrentThread.CurrentCulture = [System.Globalization.CultureInfo]::GetCultureInfo("es-ES")

# Constants for filament weight calculation
$filamentDiameter = 1.75  # mm
$density = 1  # g/cm³
$area = [Math]::PI * [Math]::Pow(($filamentDiameter / 2), 2)  # mm²
$densityPerMm3 = $density / 1000  # g/mm³
$weightFactor = $area * $densityPerMm3  # g/mm

# Get all .gcode files in the current folder
$gcodeFiles = Get-ChildItem -Path . -Filter *.gcode

# Array to store layer_times data
$allLayerTimes = @()

# Hashtable to store totals per file
$fileTotals = @{}

# For each .gcode file, run the command and process the output
foreach ($file in $gcodeFiles) {
    $baseName = $file.BaseName
    
    # Run the command and capture the output
    $output = & .\klipper_estimator.exe --config_file config.json estimate --format json $file.Name 2>&1
    Write-Host "Processing: $($file.Name)"
    
    # Parse the JSON output
    try {
        $json = $output | ConvertFrom-Json -AsHashTable
        if ($json.ContainsKey('sequences') -and $json.sequences.Count -gt 0) {
            # Use only the last sequence (aggregator)
            $lastSequence = $json.sequences[-1]

            # Compute totals from the last sequence
            $totalTime = $lastSequence.total_time / 60  # Convert to minutes
            $totalExtrude = $lastSequence.total_extrude_distance

            # Subtract specific times from kind_times if present
            if ($lastSequence.ContainsKey('kind_times')) {
                $kindTimes = $lastSequence.kind_times
                if ($kindTimes.ContainsKey('perimeter')) { $totalTime -= $kindTimes['perimeter'] / 60 }
                if ($kindTimes.ContainsKey('move to first perimeter point')) { $totalTime -= $kindTimes['move to first perimeter point'] / 60 }
                if ($kindTimes.ContainsKey('move to first infill point')) { $totalTime -= $kindTimes['move to first infill point'] / 60 }
            }

            $fileTotals[$baseName] = @{ TotalTime = $totalTime; TotalExtrude = $totalExtrude }

            # Extract layer_times from the last sequence
            if ($lastSequence.ContainsKey('layer_times')) {
                $numLayers = $lastSequence.layer_times.Count
                $perimeterTimePerLayer = 0
                if ($lastSequence.ContainsKey('kind_times') -and $lastSequence.kind_times.ContainsKey('perimeter')) {
                    $perimeterTimePerLayer = $lastSequence.kind_times['perimeter'] / $numLayers
                }
                foreach ($pair in $lastSequence.layer_times) {
                    $time = $pair[1] - $perimeterTimePerLayer
                    if ($time -lt 0) { $time = 0 }
                    $allLayerTimes += [PSCustomObject]@{
                        File = $baseName
                        ZHeight = $pair[0]
                        Time = $time
                    }
                }
            } else {
                Write-Host "The last sequence has no layer_times in $($file.Name)"
            }
        } else {
            Write-Host "No sequences found in $($file.Name)"
        }
    } catch {
        Write-Host "Error parsing JSON for $($file.Name): $_"
        Write-Host "Output: $output"
    }
}

# Save pivoted CSV
if ($allLayerTimes.Count -gt 0) {
    # Get unique files
    $files = $allLayerTimes | Select-Object -Unique -ExpandProperty File

    # Get unique sorted heights and filter to exclude first and last layer
    $heights = $allLayerTimes | Select-Object -Unique -ExpandProperty ZHeight | Sort-Object
    $heights = $heights[1..($heights.Count-2)]

    # Create hashtable for data per file
    $fileData = @{}
    foreach ($file in $files) {
        $fileData[$file] = @{}
    }

    # Fill the hashtable with times
    foreach ($item in $allLayerTimes) {
        $fileData[$item.File][$item.ZHeight] = $item.Time
    }

    # Create lines for totals table
    $tempCsvPath = "layer_times_temp.csv"
    $summaryLines = @()
    $summaryLines += "Infill;Total Time;g"
    foreach ($file in $files) {
        $totals = $fileTotals[$file]
        $weight = $totals.TotalExtrude * $weightFactor
        $weightFormatted = $weight.ToString("N", [System.Globalization.CultureInfo]::GetCultureInfo("es-ES"))
        $timeFormatted = $totals.TotalTime.ToString("N", [System.Globalization.CultureInfo]::GetCultureInfo("es-ES"))
        $summaryLines += "$file;$timeFormatted;$weightFormatted"
    }

    # Create lines for layer times table
    $layerLines = @()
    $header = "Height"
    foreach ($file in $files) { $header += ";$file" }
    $layerLines += $header
    foreach ($height in $heights) {
        $line = $height.ToString([System.Globalization.CultureInfo]::GetCultureInfo("es-ES"))
        foreach ($file in $files) {
            $time = $fileData[$file][$height]
            if ($time -eq $null -or $time -eq 0) {
                $line += ";"
            } else {
                # Keep in seconds
                $line += ";" + $time.ToString("N", [System.Globalization.CultureInfo]::GetCultureInfo("es-ES"))
            }
        }
        $layerLines += $line
    }

    # Combine all lines: totals first, then empty, then layer times
    $allLines = $summaryLines + "" + $layerLines

    # Write to the temporary file
    $allLines | Out-File -FilePath $tempCsvPath -Encoding UTF8

    # Rename the temporary file at the end with a timestamp to avoid conflicts
    $timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $finalCsvPath = "layer_times_$timestamp.csv"
    Move-Item -Path $tempCsvPath -Destination $finalCsvPath -Force
    Write-Host "Data saved to $finalCsvPath"
} else {
    Write-Host "No layer_times data found to save"
}
