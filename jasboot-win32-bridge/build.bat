@echo off

setlocal



set "ROOT=%~dp0"

set "SRC_C=%ROOT%src\jasboot_win32_bridge.c"

set "SRC_CPP=%ROOT%src\webview2_host.cpp"

set "BIN=%ROOT%bin"

set "OUT=%BIN%\jasboot_win32_bridge.dll"

set "WV2_INC=%ROOT%third_party\webview2_pkg\build\native\include"

set "WV2_PKG=%ROOT%third_party\webview2_pkg"



if not exist "%BIN%" mkdir "%BIN%"



if not exist "%WV2_INC%\WebView2.h" (

  echo ERROR: Falta WebView2 SDK en third_party\webview2_pkg

  echo Descarga el paquete NuGet Microsoft.Web.WebView2 y extrae aqui, o ejecuta:

  echo   mkdir third_party ^&^& curl -L -o third_party\wv2.zip https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.2535.41 ^&^& tar -xf third_party\wv2.zip -C third_party\webview2_pkg

  exit /b 1

)



gcc -std=c11 -O2 -c -o "%ROOT%src\jasboot_win32_bridge.o" "%SRC_C%"

if errorlevel 1 (echo ERROR: gcc fallo en .c & exit /b 1)



g++ -std=c++17 -O2 -I"%WV2_INC%" -c -o "%ROOT%src\webview2_host.o" "%SRC_CPP%"

if errorlevel 1 (echo ERROR: g++ fallo en webview2_host.cpp & exit /b 1)



g++ -shared -o "%OUT%" "%ROOT%src\jasboot_win32_bridge.o" "%ROOT%src\webview2_host.o" -lgdi32 -lole32 -luuid -lshell32 -static-libgcc

if errorlevel 1 (echo ERROR: no se pudo enlazar jasboot_win32_bridge.dll & exit /b 1)



if /I "%PROCESSOR_ARCHITECTURE%"=="AMD64" (

  copy /Y "%WV2_PKG%\runtimes\win-x64\native\WebView2Loader.dll" "%BIN%\" >nul

) else (

  copy /Y "%WV2_PKG%\runtimes\win-x86\native\WebView2Loader.dll" "%BIN%\" >nul

)

if errorlevel 1 echo AVISO: copiar WebView2Loader.dll a bin fallo



echo OK: %OUT%

endlocal

