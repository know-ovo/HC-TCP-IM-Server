param(
    [string]$ContainerName = "my-mysql",
    [string]$RootPassword = "123456",
    [string]$SqlFile = "sql/001_m2_reliable_message.sql"
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$sqlPath = Join-Path $projectRoot $SqlFile

if (-not (Test-Path $sqlPath)) {
    throw "SQL file not found: $sqlPath"
}

Write-Host "Copy schema into container: $ContainerName"
docker cp $sqlPath "${ContainerName}:/tmp/001_m2_reliable_message.sql"

Write-Host "Import schema"
docker exec $ContainerName sh -c "mysql -uroot -p$RootPassword < /tmp/001_m2_reliable_message.sql"

Write-Host "Show created tables"
$showTablesCmd = "mysql -uroot -p$RootPassword -D im_server -e 'SHOW TABLES;'"
docker exec $ContainerName sh -c $showTablesCmd
