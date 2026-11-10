# Prepare payload for phide offline installer
$ErrorActionPreference = "Stop"

$repoRoot = (Get-Item $PSScriptRoot).Parent.FullName
$payload = Join-Path $PSScriptRoot "payload"

Write-Host "======================================================================"
Write-Host " Staging payload for phide all-in-one offline installer"
Write-Host "======================================================================"

# 1. Clean and initialize payload directories
if (Test-Path $payload) {
    Remove-Item -Recurse -Force $payload
}
New-Item -ItemType Directory -Path "$payload\assets", "$payload\include\phide", "$payload\runtimes\php", "$payload\runtimes\mysql\bin", "$payload\runtimes\mysql\data", "$payload\runtimes\mysql\initial_data", "$payload\runtimes\mysql\share" -Force | Out-Null

# 2. Copy main application files
Write-Host "[1/6] Staging phide.exe and snippets..."
Copy-Item "$repoRoot\bin\phide.exe" "$payload\phide.exe" -Force
if (Test-Path "$repoRoot\snippets.json") {
    Copy-Item "$repoRoot\snippets.json" "$payload\snippets.json" -Force
}

# 3. Copy assets (fonts, icons, extensions, learning hub)
Write-Host "[2/6] Staging assets directory..."
Copy-Item "$repoRoot\assets\*" "$payload\assets\" -Recurse -Force

# 4. Copy include\phide (Nginx and phpMyAdmin)
Write-Host "[3/6] Staging server (Nginx) and database (phpMyAdmin)..."
Copy-Item "$repoRoot\include\phide\*" "$payload\include\phide\" -Recurse -Force

# Clean any stale logs and temp files from staged Nginx
if (Test-Path "$payload\include\phide\server\nginx\logs") {
    Get-ChildItem -Path "$payload\include\phide\server\nginx\logs" -Filter "*.log" | Remove-Item -Force -ErrorAction SilentlyContinue
}
if (Test-Path "$payload\include\phide\server\nginx\temp") {
    Get-ChildItem -Path "$payload\include\phide\server\nginx\temp" | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
}

# 5. Stage bundled PHP 8.5 runtime and Composer
Write-Host "[4/6] Staging bundled PHP 8.5 runtime and Composer..."
$phpSource = "C:\Users\Administrator\AppData\Local\Programs\PHP\v8.5"
if (Test-Path $phpSource) {
    Copy-Item "$phpSource\*" "$payload\runtimes\php\" -Recurse -Force
    # Make php.ini relative and portable without BOM
    $phpIniPath = "$payload\runtimes\php\php.ini"
    if (Test-Path $phpIniPath) {
        $content = [System.IO.File]::ReadAllText($phpIniPath)
        $content = $content -replace 'extension_dir\s*=\s*".*?\\ext"', 'extension_dir = "ext"'
        # Explicitly disable opcache to prevent Windows ASLR fatal crashes
        $content = $content -replace ';?opcache\.enable\s*=.*', 'opcache.enable=0'
        $content = $content -replace ';?opcache\.enable_cli\s*=.*', 'opcache.enable_cli=0'
        $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
        [System.IO.File]::WriteAllText($phpIniPath, $content, $utf8NoBom)
    }
} else {
    Write-Warning "PHP source not found at $phpSource"
}

# 6. Stage bundled MySQL / MariaDB runtime with FULL active user database
Write-Host "[5/6] Staging bundled MariaDB / MySQL runtime and user databases..."
$mysqlSource = "C:\xampp\mysql"
if (Test-Path $mysqlSource) {
    Copy-Item "$mysqlSource\bin\mysqld.exe", "$mysqlSource\bin\mysql.exe", "$mysqlSource\bin\mysqladmin.exe", "$mysqlSource\bin\*.dll" "$payload\runtimes\mysql\bin\" -Force
    Copy-Item "$mysqlSource\share\*" "$payload\runtimes\mysql\share\" -Recurse -Force
    
    # Copy full database directory (including user databases auth, crud, student_crud, public, phpmyadmin, mysql, user_test, user_sd, etc.)
    # Exclude transient files: mysql.pid, ibtmp1, *.log, *.dmp, *.err
    $excludeItems = @("mysql.pid", "ibtmp1", "mysql_error.log", "mysqld.dmp")
    Get-ChildItem -Path "$mysqlSource\data" | Where-Object { 
        $excludeItems -notcontains $_.Name -and $_.Extension -ne ".err" -and $_.Extension -ne ".dmp"
    } | ForEach-Object {
        Copy-Item -Path $_.FullName -Destination "$payload\runtimes\mysql\data\" -Recurse -Force
        Copy-Item -Path $_.FullName -Destination "$payload\runtimes\mysql\initial_data\" -Recurse -Force
    }
    
    # Create clean, portable my.ini WITHOUT BOM
    $myIniPath = "$payload\runtimes\mysql\bin\my.ini"
    $origIniPath = "$mysqlSource\bin\my.ini"
    if (Test-Path $origIniPath) {
        $lines = [System.IO.File]::ReadAllLines($origIniPath)
        $cleanLines = [System.Collections.Generic.List[string]]::new()
        foreach ($line in $lines) {
            $trimmed = $line.Trim()
            if ($trimmed -match '^(basedir|datadir|tmpdir|innodb_data_home_dir|innodb_log_group_home_dir|innodb_log_arch_dir|plugin_dir|socket)\b') {
                continue
            }
            if ($trimmed -eq 'key_buffer=16M') {
                $cleanLines.Add('key_buffer_size=16M')
                continue
            }
            $cleanLines.Add($line)
        }
        $asciiEnc = [System.Text.Encoding]::ASCII
        [System.IO.File]::WriteAllLines($myIniPath, $cleanLines, $asciiEnc)
    }
} else {
    Write-Warning "MySQL source not found at $mysqlSource"
}

# 7. Stage VC++ CRT DLLs for maximum compatibility
Write-Host "[6/6] Staging VC++ runtime DLLs..."
$crtFiles = @("concrt140.dll", "msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll", "ucrtbase.dll", "vcruntime140.dll", "vcruntime140_1.dll")
foreach ($crt in $crtFiles) {
    $src = "$mysqlSource\bin\$crt"
    if (Test-Path $src) {
        Copy-Item $src "$payload\$crt" -Force
    }
}

$measure = Get-ChildItem -Path $payload -Recurse | Measure-Object -Property Length -Sum
$sizeMB = [math]::Round($measure.Sum / 1MB, 2)
Write-Host "======================================================================"
Write-Host " Staged payload total size: $sizeMB MB across $($measure.Count) files"
Write-Host "======================================================================"
