@echo off
REM Test Nivel 5: Escala y particionado (indice maestro, n_heredar particionado/shard)
gcc -Wall -I. n_grafo_core.c n_grafo_particionado.c n_grafo_indice.c n_grafo_shard.c test_nivel5.c -o test_nivel5.exe
if %errorlevel% neq 0 exit /b 1
test_nivel5.exe
