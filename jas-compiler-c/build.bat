@echo off
setlocal
set "INC=-Iinclude"
gcc -std=c11 -Wall %INC% -c src/diagnostic.c -o src/diagnostic.o
gcc -std=c11 -Wall %INC% -c src/keywords.c -o src/keywords.o
gcc -std=c11 -Wall %INC% -c src/sistema_llamadas.c -o src/sistema_llamadas.o
gcc -std=c11 -Wall %INC% -c src/token_vec.c -o src/token_vec.o
gcc -std=c11 -Wall %INC% -c src/lexer.c -o src/lexer.o
gcc -std=c11 -Wall %INC% -c src/nodes.c -o src/nodes.o
gcc -std=c11 -Wall %INC% -c src/parser.c -o src/parser.o
gcc -std=c11 -Wall %INC% -c src/symbol_table.c -o src/symbol_table.o
gcc -std=c11 -Wall %INC% -c src/resolve.c -o src/resolve.o
gcc -std=c11 -Wall %INC% -c src/codegen.c -o src/codegen.o
gcc -std=c11 -Wall -DJBC_MINIMAL_MAIN %INC% -c src/main.c -o src/main.o
if not exist "bin" mkdir "bin"
gcc -o "bin\jbc.exe" src/diagnostic.o src/keywords.o src/sistema_llamadas.o src/token_vec.o src/lexer.o src/nodes.o src/parser.o src/symbol_table.o src/resolve.o src/codegen.o src/main.o
if %errorlevel% neq 0 (echo Build FAILED & exit /b 1)
echo Compilador: %cd%\bin\jbc.exe

REM CLI en raiz: launcher pequeno -> reenvia a sdk-dependiente\jas-compiler-c\bin\jbc.exe
if not exist "..\..\bin" mkdir "..\..\bin"
gcc -std=c11 -Wall -O2 -o "..\..\bin\jbc.exe" jbc_launcher.c
if %errorlevel% equ 0 (
  echo Launcher CLI: ..\..\bin\jbc.exe  ^-^>  sdk-dependiente\jas-compiler-c\bin\jbc.exe
  if exist "..\..\bin\jbc_next.exe" del "..\..\bin\jbc_next.exe" >nul 2>&1
  goto :root_cli_done
)
echo ADVERTENCIA: no se pudo escribir ..\..\bin\jbc.exe ^(en uso?^). Probando jbc_next.exe...
gcc -std=c11 -Wall -O2 -o "..\..\bin\jbc_next.exe" jbc_launcher.c
if %errorlevel% equ 0 (
  echo Listo: ..\..\bin\jbc_next.exe  ^-^>  sdk-dependiente\jas-compiler-c\bin\jbc.exe
  echo Cierre terminales/IDE que usen jbc.exe y renombre jbc_next.exe a jbc.exe
  goto :root_cli_done
)
echo ADVERTENCIA: launcher fallo. Copiando compilador completo a ..\..\bin\jbc.exe...
copy /Y "bin\jbc.exe" "..\..\bin\jbc.exe" >nul 2>&1
if %errorlevel% neq 0 echo ADVERTENCIA: tampoco se pudo copiar; use:  %cd%\bin\jbc.exe
:root_cli_done

if not exist "..\bin" mkdir "..\bin"
copy /Y "bin\jbc.exe" "..\bin\jbc.exe" >nul 2>&1
endlocal
