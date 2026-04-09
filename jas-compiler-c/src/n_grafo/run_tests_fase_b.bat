@echo off
REM Tests Fase B: particionado, indice texto, compresion, cache
cd /d "%~dp0"
gcc -Wall -I. n_grafo_core.c n_grafo_particionado.c n_grafo_indice_texto.c n_grafo_compresion.c n_grafo_cache.c test_fase_b.c -o test_fase_b.exe
if %errorlevel% neq 0 (echo Build FAILED & exit /b 1)
test_fase_b.exe
set R=%errorlevel%
if exist test_geo.ngf del test_geo.ngf
if exist test_bio.ngf del test_bio.ngf
exit /b %R%
