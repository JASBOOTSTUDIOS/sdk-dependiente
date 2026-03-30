@echo off
REM Borra artefactos de compilacion (no borra fuentes ni .cmd en bin\).
setlocal
for %%I in ("%~dp0..") do set "R=%%~fI\"
rmdir /s /q "%R%jasboot-ir\build" 2>nul
del /q "%R%jasboot-ir\bin\*.exe" 2>nul
del /q "%R%jas-compiler-c\src\*.o" 2>nul
del /q "%R%jas-compiler-c\bin\*.exe" 2>nul
del /q "%R%bin\*.exe" 2>nul
echo Limpieza de objetos y ejecutables generados terminada.
endlocal
