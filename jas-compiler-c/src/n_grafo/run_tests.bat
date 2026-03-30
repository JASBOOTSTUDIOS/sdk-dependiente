@echo off
REM Tests n_grafo (Fases 1-9) + estrés
cd /d "%~dp0"
gcc -Wall -I. n_grafo_core.c test_n_grafo.c -o test_n_grafo.exe
if %errorlevel% neq 0 (echo Build FAILED & exit /b 1)
test_n_grafo.exe
if %errorlevel% neq 0 exit /b 1
gcc -Wall -I. n_grafo_core.c test_n_grafo_estres.c -o test_n_grafo_estres.exe
if %errorlevel% neq 0 (echo Build estres FAILED & exit /b 1)
test_n_grafo_estres.exe
set R=%errorlevel%
if exist test_n_grafo.ngf del test_n_grafo.ngf
if exist test_n_grafo_estres.ngf del test_n_grafo_estres.ngf
exit /b %R%
