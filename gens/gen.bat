@echo off
call "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Enterprise\MSBuild\Current\Bin\amd64\MSBuild.exe"
call "pkggen\bin\x64\Debug\pkggen.exe"
if exist output\PKG_TypeIdMappings.cs (
	xcopy output\PKG_TypeIdMappings.cs pkggen_template_PKG\ /y
	call "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Enterprise\MSBuild\Current\Bin\amd64\MSBuild.exe"
	call "pkggen\bin\x64\Debug\pkggen.exe"
	del output\PKG_TypeIdMappings.cs
)
call "%ProgramFiles(x86)%\Microsoft Visual Studio\2019\Enterprise\MSBuild\Current\Bin\amd64\MSBuild.exe"
call "cfg_maker\bin\x64\Debug\cfg_maker.exe"
pause