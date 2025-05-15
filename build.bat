@echo off

:: Define paths
set SOURCE_DIR=%~dp0
set BUILD_DIR=%SOURCE_DIR%build
set INSTALL_DIR=%BUILD_DIR%\install

:: Build appbox
set APPBOX_SOURCE_DIR=%SOURCE_DIR%\appbox
:: Build 32 bit dll
set APPBOX32_BUILD_DIR=%BUILD_DIR%\appbox32
cmake -B %APPBOX32_BUILD_DIR% -DAPPBOX_DLL_SUFFIX:STRING=32 -DCMAKE_INSTALL_PREFIX:PATH=%INSTALL_DIR% -A Win32 %APPBOX_SOURCE_DIR%
if %errorlevel% neq 0 exit /b %errorlevel%
cmake --build %APPBOX32_BUILD_DIR% --config Release
if %errorlevel% neq 0 exit /b %errorlevel%
cmake --install %APPBOX32_BUILD_DIR% --config Release
if %errorlevel% neq 0 exit /b %errorlevel%
:: Build 64 bit dll
set APPBOX64_BUILD_DIR=%BUILD_DIR%\appbox64
cmake -B %APPBOX64_BUILD_DIR% -DAPPBOX_DLL_SUFFIX:STRING=64 -DCMAKE_INSTALL_PREFIX:PATH=%INSTALL_DIR% -A x64 %APPBOX_SOURCE_DIR%
if %errorlevel% neq 0 exit /b %errorlevel%
cmake --build %APPBOX64_BUILD_DIR% --config Release
if %errorlevel% neq 0 exit /b %errorlevel%
cmake --install %APPBOX64_BUILD_DIR% --config Release
if %errorlevel% neq 0 exit /b %errorlevel%

:: Build loader
set LOADER_SOURCE_DIR=%SOURCE_DIR%\loader
set LOADER_BUILD_DIR=%BUILD_DIR%\loader
cmake -B %LOADER_BUILD_DIR% -DCMAKE_INSTALL_PREFIX:PATH=%INSTALL_DIR% -A Win32 %LOADER_SOURCE_DIR%
if %errorlevel% neq 0 exit /b %errorlevel%
cmake --build %LOADER_BUILD_DIR% --config Release
if %errorlevel% neq 0 exit /b %errorlevel%
cmake --install %LOADER_BUILD_DIR% --config Release
if %errorlevel% neq 0 exit /b %errorlevel%

:: Build packer
set PACKER_SOURCE_DIR=%SOURCE_DIR%\packer
set PACKER_BUILD_DIR=%BUILD_DIR%\packer
cmake -B %PACKER_BUILD_DIR% -DCMAKE_INSTALL_PREFIX:PATH=%INSTALL_DIR% -A Win32 %PACKER_SOURCE_DIR%
if %errorlevel% neq 0 exit /b %errorlevel%
cmake --build %PACKER_BUILD_DIR% --config Release
if %errorlevel% neq 0 exit /b %errorlevel%
cmake --install %PACKER_BUILD_DIR% --config Release
if %errorlevel% neq 0 exit /b %errorlevel%

:: Build GUI
set GUI_SOURCE_DIR=%SOURCE_DIR%\gui
set GUI_BUILD_DIR=%BUILD_DIR%\gui
cmake -B %GUI_BUILD_DIR% -DCMAKE_INSTALL_PREFIX:PATH=%INSTALL_DIR% -A Win32 %GUI_SOURCE_DIR%
if %errorlevel% neq 0 exit /b %errorlevel%
cmake --build %GUI_BUILD_DIR% --config Release
if %errorlevel% neq 0 exit /b %errorlevel%
cmake --install %GUI_BUILD_DIR% --config Release
if %errorlevel% neq 0 exit /b %errorlevel%
