$inp = @($input)
$inp | ForEach-Object { $_ -replace '%{!shared:%:if-exists(default-manifest.o%s)}', '' } | Out-File -Encoding ASCII no_manifest.spec
