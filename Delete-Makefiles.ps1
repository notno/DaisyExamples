# Define the root directory where the search will begin.
# Replace this path with your target directory.
$rootPath = Get-Location

# Define the directory name to exclude (case-insensitive)
$excludedDirName = "build"

# Perform a dry run first to see which files would be deleted
# Comment out the Write-Host line and uncomment Remove-Item after verifying the dry run

Get-ChildItem -Path $rootPath -Recurse -Filter "Makefile" -File | Where-Object {
    # Initialize a flag to determine if the file is within an excluded directory
    $isInExcludedDir = $false

    # Start with the directory containing the file
    $currentDir = $_.Directory

    # Traverse up the directory tree
    while ($currentDir -ne $null) {
        if ($currentDir.Name -ieq $excludedDirName) {
            $isInExcludedDir = $true
            break
        }
        $currentDir = $currentDir.Parent
    }

    # Return true if the file is NOT in an excluded directory
    -not $isInExcludedDir
} | ForEach-Object {
    try {
        # Uncomment the following line after confirming the dry run
        Remove-Item -Path $_.FullName -Force -ErrorAction Stop

        # For dry run: Display the files that would be deleted
        Write-Host "deleting: $($_.FullName)" -ForegroundColor Yellow
    }
    catch {
        Write-Warning "Failed to delete: $($_.FullName). Error: $_"
    }
}
