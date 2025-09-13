# Script para ejecutar klipper_estimator.exe en todos los archivos .gcode de la carpeta y guardar layer_times en CSV

# Configurar cultura española para formato de números y delimitador CSV
[System.Threading.Thread]::CurrentThread.CurrentCulture = [System.Globalization.CultureInfo]::GetCultureInfo("es-ES")

# Constantes para cálculo de peso del filamento
$filamentDiameter = 1.75  # mm
$density = 1  # g/cm³
$area = [Math]::PI * [Math]::Pow(($filamentDiameter / 2), 2)  # mm²
$densityPerMm3 = $density / 1000  # g/mm³
$weightFactor = $area * $densityPerMm3  # g/mm

# Obtener todos los archivos .gcode en la carpeta actual
$gcodeFiles = Get-ChildItem -Path . -Filter *.gcode

# Array para almacenar los datos de layer_times
$allLayerTimes = @()

# Hashtable para almacenar totales por archivo
$fileTotals = @{}

# Para cada archivo .gcode, ejecutar el comando y procesar la salida
foreach ($file in $gcodeFiles) {
    $baseName = $file.BaseName
    
    # Ejecutar el comando y capturar la salida
    $output = & .\klipper_estimator.exe --config_file config.json estimate --format json $file.Name 2>&1
    Write-Host "Procesando: $($file.Name)"
    
    # Parsear la salida JSON
    try {
        $json = $output | ConvertFrom-Json -AsHashTable
        if ($json.ContainsKey('sequences') -and $json.sequences.Count -gt 0) {
            # Usar solo la última sequence (totalizadora)
            $lastSequence = $json.sequences[-1]
            
            # Calcular totales de la última sequence
            $totalTime = $lastSequence.total_time / 60  # Convertir a minutos
            $totalExtrude = $lastSequence.total_extrude_distance
            
            # Restar tiempos específicos de kind_times si existen
            if ($lastSequence.ContainsKey('kind_times')) {
                $kindTimes = $lastSequence.kind_times
                if ($kindTimes.ContainsKey('perimeter')) { $totalTime -= $kindTimes['perimeter'] / 60 }
                if ($kindTimes.ContainsKey('move to first perimeter point')) { $totalTime -= $kindTimes['move to first perimeter point'] / 60 }
                if ($kindTimes.ContainsKey('move to first infill point')) { $totalTime -= $kindTimes['move to first infill point'] / 60 }
            }
            
            $fileTotals[$baseName] = @{ TotalTime = $totalTime; TotalExtrude = $totalExtrude }
            
            # Extraer layer_times de la última sequence
            if ($lastSequence.ContainsKey('layer_times')) {
                foreach ($pair in $lastSequence.layer_times) {
                    $allLayerTimes += [PSCustomObject]@{
                        File = $baseName
                        ZHeight = $pair[0]
                        Time = $pair[1]
                    }
                }
            } else {
                Write-Host "La última sequence no tiene layer_times en $($file.Name)"
            }
        } else {
            Write-Host "No se encontraron sequences en $($file.Name)"
        }
    } catch {
        Write-Host "Error al parsear JSON para $($file.Name): $_"
        Write-Host "Salida: $output"
    }
}

# Guardar en CSV pivotado
if ($allLayerTimes.Count -gt 0) {
    # Obtener archivos únicos
    $files = $allLayerTimes | Select-Object -Unique -ExpandProperty File
    
    # Obtener alturas únicas ordenadas
    $heights = $allLayerTimes | Select-Object -Unique -ExpandProperty ZHeight | Sort-Object
    
    # Crear hashtable para datos por archivo
    $fileData = @{}
    foreach ($file in $files) {
        $fileData[$file] = @{}
    }
    
    # Llenar el hashtable con tiempos
    foreach ($item in $allLayerTimes) {
        $fileData[$item.File][$item.ZHeight] = $item.Time
    }
    
    # Crear datos para CSV
    $csvData = @()
    foreach ($height in $heights) {
        $row = [PSCustomObject]@{ Height = $height }
        foreach ($file in $files) {
            $time = $fileData[$file][$height]
            if ($time -eq $null) { $time = 0 }  # Usar 0 si no hay tiempo para esa altura
            $row | Add-Member -MemberType NoteProperty -Name $file -Value $time
        }
        $csvData += $row
    }
    
    # Crear líneas para la tabla de totales
    $tempCsvPath = "layer_times_temp.csv"
    $summaryLines = @()
    $summaryLines += "infill;total time (min);g"
    foreach ($file in $files) {
        $totals = $fileTotals[$file]
        $weight = $totals.TotalExtrude * $weightFactor
        $weightFormatted = $weight.ToString("N", [System.Globalization.CultureInfo]::GetCultureInfo("es-ES"))
        $timeFormatted = $totals.TotalTime.ToString("N", [System.Globalization.CultureInfo]::GetCultureInfo("es-ES"))
        $summaryLines += "$file;$timeFormatted;$weightFormatted"
    }
    
    # Crear líneas para la tabla de layer times
    $layerLines = @()
    $header = "Height"
    foreach ($file in $files) { $header += ";$file" }
    $layerLines += $header
    foreach ($height in $heights) {
        $line = $height.ToString([System.Globalization.CultureInfo]::GetCultureInfo("es-ES"))
        foreach ($file in $files) {
            $time = $fileData[$file][$height]
            if ($time -eq $null) { $time = 0 }
            # Mantener en segundos
            $line += ";" + $time.ToString("N", [System.Globalization.CultureInfo]::GetCultureInfo("es-ES"))
        }
        $layerLines += $line
    }
    
    # Combinar todas las líneas: totales primero, luego vacía, luego layer times
    $allLines = $summaryLines + "" + $layerLines
    
    # Escribir al archivo temporal
    $allLines | Out-File -FilePath $tempCsvPath -Encoding UTF8
    
    # Renombrar el archivo temporal al final con timestamp para evitar conflictos
    $timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $finalCsvPath = "layer_times_$timestamp.csv"
    Move-Item -Path $tempCsvPath -Destination $finalCsvPath -Force
    Write-Host "Datos guardados en $finalCsvPath"
} else {
    Write-Host "No se encontraron datos de layer_times para guardar"
}
