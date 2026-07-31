:: link_libs.bat

:: First, remove old folders/links if any remain from previous attempts
rmdir /S /Q include
rmdir /S /Q lib\x86_64-win32

:: Create the folder structure
mkdir lib\x86_64-win32

:: Create symbolic links
:: This line mirrors the include folder from your built Box3D MSVC directory
mklink /D include "C:\libs\box3d-0.1.0-release-msvc\include"

:: This line links the compiled Box3D MSVC static library (.lib) for Defold's Windows target
mklink lib\x86_64-win32\box3d.lib "C:\libs\box3d-0.1.0-release-msvc\lib\box3d.lib"