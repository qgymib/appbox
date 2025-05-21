@echo off

:: Define paths
set SOURCE_DIR=%~dp0
set BUILD_TYPE=Debug
set BUILD_DIR=%SOURCE_DIR%build\%BUILD_TYPE%
set INSTALL_DIR=%BUILD_DIR%\install

:: third_party libraries
:: json
set JSON_SOURCE_DIR=%SOURCE_DIR%\third_party\json
set JSON_BUILD_DIR=%BUILD_DIR%\json32
cmake -B %JSON_BUILD_DIR% -DCMAKE_INSTALL_PREFIX:PATH=%INSTALL_DIR%\Win32 -DJSON_BuildTests=OFF -A Win32 %JSON_SOURCE_DIR%
if %errorlevel% neq 0 exit /b %errorlevel%
cmake --build %JSON_BUILD_DIR% --config %BUILD_TYPE%
if %errorlevel% neq 0 exit /b %errorlevel%
cmake --install %JSON_BUILD_DIR% --config %BUILD_TYPE%
if %errorlevel% neq 0 exit /b %errorlevel%
::spdlog
set SPDLOG_SOURCE_DIR=%SOURCE_DIR%\third_party\spdlog
set SPDLOG_BUILD_DIR=%BUILD_DIR%\spdlog32
cmake -B %SPDLOG_BUILD_DIR% -DCMAKE_INSTALL_PREFIX:PATH=%INSTALL_DIR%\Win32 -DSPDLOG_BUILD_EXAMPLE=OFF -DSPDLOG_WCHAR_SUPPORT=ON -DSPDLOG_WCHAR_FILENAMES=ON -DSPDLOG_WCHAR_CONSOLE=ON -A Win32 %SPDLOG_SOURCE_DIR%
if %errorlevel% neq 0 exit /b %errorlevel%
cmake --build %SPDLOG_BUILD_DIR% --config %BUILD_TYPE%
if %errorlevel% neq 0 exit /b %errorlevel%
cmake --install %SPDLOG_BUILD_DIR% --config %BUILD_TYPE%
if %errorlevel% neq 0 exit /b %errorlevel%
:: wxWidgets
set WXWIDGETS_SOURCE_DIR=%SOURCE_DIR%\third_party\wxWidgets
set WXWIDGETS_BUILD_DIR=%BUILD_DIR%\wxWidgets32
cmake -B %WXWIDGETS_BUILD_DIR% -DCMAKE_INSTALL_PREFIX:PATH=%INSTALL_DIR%\Win32 -DwxBUILD_SHARED=OFF -DwxBUILD_TESTS=OFF -DwxBUILD_SAMPLES=OFF -DwxBUILD_DEMOS=OFF -DwxBUILD_PRECOMP=OFF -A Win32 %WXWIDGETS_SOURCE_DIR%
if %errorlevel% neq 0 exit /b %errorlevel%
cmake --build %WXWIDGETS_BUILD_DIR% --config %BUILD_TYPE%
if %errorlevel% neq 0 exit /b %errorlevel%
cmake --install %WXWIDGETS_BUILD_DIR% --config %BUILD_TYPE%
if %errorlevel% neq 0 exit /b %errorlevel%
:: zlib
set ZLIB_SOURCE_DIR=%SOURCE_DIR%\third_party\zlib
set ZLIB_BUILD_DIR=%BUILD_DIR%\zlib32
cmake -B %ZLIB_BUILD_DIR% -DCMAKE_INSTALL_PREFIX:PATH=%INSTALL_DIR%\Win32 -DZLIB_BUILD_TESTING=OFF -DZLIB_BUILD_SHARED=OFF -DZLIB_INSTALL_COMPAT_DLL=OFF -A Win32 %ZLIB_SOURCE_DIR%
if %errorlevel% neq 0 exit /b %errorlevel%
cmake --build %ZLIB_BUILD_DIR% --config %BUILD_TYPE%
if %errorlevel% neq 0 exit /b %errorlevel%
cmake --install %ZLIB_BUILD_DIR% --config %BUILD_TYPE%
if %errorlevel% neq 0 exit /b %errorlevel%

:: Sandbox
set SANDBOX_SOURCE_DIR=%SOURCE_DIR%\sandbox
:: 32-bit dll
set SANDBOX32_BUILD_DIR=%BUILD_DIR%\sandbox32
cmake -B %SANDBOX32_BUILD_DIR% -DAPPBOX_SANDBOX_SUFFIX:STRING=32 -DCMAKE_INSTALL_PREFIX:PATH=%INSTALL_DIR%\dist -A Win32 %SANDBOX_SOURCE_DIR%
if %errorlevel% neq 0 exit /b %errorlevel%
cmake --build %SANDBOX32_BUILD_DIR% --config %BUILD_TYPE%
if %errorlevel% neq 0 exit /b %errorlevel%
cmake --install %SANDBOX32_BUILD_DIR% --config %BUILD_TYPE%
if %errorlevel% neq 0 exit /b %errorlevel%
:: Build 64 bit dll
set SANDBOX64_BUILD_DIR=%BUILD_DIR%\sandbox64
cmake -B %SANDBOX64_BUILD_DIR% -DAPPBOX_SANDBOX_SUFFIX:STRING=64 -DCMAKE_INSTALL_PREFIX:PATH=%INSTALL_DIR%\dist -A x64 %SANDBOX_SOURCE_DIR%
if %errorlevel% neq 0 exit /b %errorlevel%
cmake --build %SANDBOX64_BUILD_DIR% --config %BUILD_TYPE%
if %errorlevel% neq 0 exit /b %errorlevel%
cmake --install %SANDBOX64_BUILD_DIR% --config %BUILD_TYPE%
if %errorlevel% neq 0 exit /b %errorlevel%

:: Loader
set LOADER_SOURCE_DIR=%SOURCE_DIR%\loader
set LOADER_BUILD_DIR=%BUILD_DIR%\loader32
cmake -B %LOADER_BUILD_DIR% -DCMAKE_INSTALL_PREFIX:PATH=%INSTALL_DIR%\dist -DJSON_SEARCH_PATH=%INSTALL_DIR%\Win32 -DSPDLOG_SEARCH_PATH=%INSTALL_DIR%\Win32 -DWXWIDGETS_SEARCH_PATH=%INSTALL_DIR%\Win32 -DZLIB_SEARCH_PATH=%INSTALL_DIR%\Win32 -A Win32 %LOADER_SOURCE_DIR%
if %errorlevel% neq 0 exit /b %errorlevel%
cmake --build %LOADER_BUILD_DIR% --config %BUILD_TYPE%
if %errorlevel% neq 0 exit /b %errorlevel%
cmake --install %LOADER_BUILD_DIR% --config %BUILD_TYPE%
if %errorlevel% neq 0 exit /b %errorlevel%

:: Packer
set PACKER_SOURCE_DIR=%SOURCE_DIR%\packer
set PACKER_BUILD_DIR=%BUILD_DIR%\packer32
cmake -B %PACKER_BUILD_DIR% -DCMAKE_INSTALL_PREFIX:PATH=%INSTALL_DIR%\dist -DJSON_SEARCH_PATH=%INSTALL_DIR%\Win32 -DSPDLOG_SEARCH_PATH=%INSTALL_DIR%\Win32 -DWXWIDGETS_SEARCH_PATH=%INSTALL_DIR%\Win32 -DZLIB_SEARCH_PATH=%INSTALL_DIR%\Win32 -A Win32 %PACKER_SOURCE_DIR%
if %errorlevel% neq 0 exit /b %errorlevel%
cmake --build %PACKER_BUILD_DIR% --config %BUILD_TYPE%
if %errorlevel% neq 0 exit /b %errorlevel%
cmake --install %PACKER_BUILD_DIR% --config %BUILD_TYPE%
if %errorlevel% neq 0 exit /b %errorlevel%
