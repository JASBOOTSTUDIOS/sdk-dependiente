@echo off
setlocal
set "INC=-Iinclude"
set "IROPT=-I../jasboot-ir/src -O2"
gcc -std=c11 -Wall %INC% %IROPT% -c src/diagnostic.c -o src/diagnostic.o
gcc -std=c11 -Wall %INC% %IROPT% -c src/keywords.c -o src/keywords.o
gcc -std=c11 -Wall %INC% %IROPT% -c src/sistema_llamadas.c -o src/sistema_llamadas.o
gcc -std=c11 -Wall %INC% %IROPT% -c src/token_vec.c -o src/token_vec.o
gcc -std=c11 -Wall %INC% %IROPT% -c src/lexer.c -o src/lexer.o
gcc -std=c11 -Wall %INC% %IROPT% -c src/nodes.c -o src/nodes.o
gcc -std=c11 -Wall %INC% %IROPT% -c src/parser.c -o src/parser.o
gcc -std=c11 -Wall %INC% %IROPT% -c src/symbol_table.c -o src/symbol_table.o
gcc -std=c11 -Wall %INC% %IROPT% -c src/resolve.c -o src/resolve.o
gcc -std=c11 -Wall %INC% %IROPT% -c src/codegen.c -o src/codegen.o
gcc -std=c11 -Wall %INC% %IROPT% -c ../jasboot-ir/src/ir_format.c -o src/ir_format.o
gcc -std=c11 -Wall %INC% %IROPT% -c ../jasboot-ir/src/optimizer_ir.c -o src/optimizer_ir.o
gcc -std=c11 -Wall %INC% %IROPT% -c src/jbc_ir_opt.c -o src/jbc_ir_opt.o
gcc -std=c11 -Wall -O2 -c src/win_spawn.c -o src/win_spawn.o
gcc -std=c11 -Wall -DJBC_MINIMAL_MAIN %INC% %IROPT% -c src/main.c -o src/main.o
if not exist "bin" mkdir "bin"
set "JBC_OBJS=src/diagnostic.o src/keywords.o src/sistema_llamadas.o src/token_vec.o src/lexer.o src/nodes.o src/parser.o src/symbol_table.o src/resolve.o src/codegen.o src/ir_format.o src/optimizer_ir.o src/jbc_ir_opt.o src/win_spawn.o src/main.o"
gcc -o "bin\jbc.exe" %JBC_OBJS%
if %errorlevel% neq 0 (
  echo AVISO: no se pudo escribir bin\jbc.exe ^(posible bloqueo^). Enlazando bin\jbc-cursor.exe...
  gcc -o "bin\jbc-cursor.exe" %JBC_OBJS%
  if %errorlevel% neq 0 (echo Build FAILED & exit /b 1)
  copy /Y "bin\jbc-cursor.exe" "bin\jbc-next.exe" >nul 2>&1
  echo OK: %cd%\bin\jbc-cursor.exe ^(run-jasb.cjs prefiere el mas reciente entre jbc-cursor/jbc/jbc-next^)
) else (
  copy /Y "bin\jbc.exe" "bin\jbc-next.exe" >nul 2>&1
  if errorlevel 1 echo AVISO: no se pudo refrescar bin\jbc-next.exe
  echo Compilador: %cd%\bin\jbc.exe
)

REM Launcher opcional (monorepo jasboot). JASBOOT_MONOREPO_BIN con barra invertida final, p. ej. C:\jasboot\bin\
if defined JASBOOT_MONOREPO_BIN (
  if not exist "%JASBOOT_MONOREPO_BIN%" mkdir "%JASBOOT_MONOREPO_BIN%"
  gcc -std=c11 -Wall -O2 -o "%JASBOOT_MONOREPO_BIN%jbc.exe" jbc_launcher.c
  if errorlevel 1 (
    echo ADVERTENCIA: launcher fallo; probando jbc_next.exe...
    gcc -std=c11 -Wall -O2 -o "%JASBOOT_MONOREPO_BIN%jbc_next.exe" jbc_launcher.c
  ) else (
    echo Launcher monorepo: %JASBOOT_MONOREPO_BIN%jbc.exe
  )
)

REM Copia al bin del repo sdk-dependiente (..\bin desde jas-compiler-c)
if not exist "..\bin" mkdir "..\bin"
if exist "bin\jbc.exe" (
  copy /Y "bin\jbc.exe" "..\bin\jbc.exe" >nul 2>&1
) else if exist "bin\jbc-cursor.exe" (
  copy /Y "bin\jbc-cursor.exe" "..\bin\jbc.exe" >nul 2>&1
)
if errorlevel 1 echo AVISO: no se pudo copiar compilador a ..\bin

REM scripts\build-compiler.bat: JASBOOT_SDK_ROOT con barra final; copia jbc al bin del SDK.
if defined JASBOOT_SDK_ROOT (
  if not exist "%JASBOOT_SDK_ROOT%bin" mkdir "%JASBOOT_SDK_ROOT%bin"
  copy /Y "bin\jbc.exe" "%JASBOOT_SDK_ROOT%bin\jbc.exe" >nul 2>&1
  if errorlevel 1 (echo AVISO: copia a JASBOOT_SDK_ROOT\bin fallo) else (echo SDK bin: %JASBOOT_SDK_ROOT%bin\jbc.exe)
)
endlocal
