@echo off
REM Tests Matemáticas 3D: trig, exp/log, mat4, mat3
setlocal
set "ROOT=%~dp0..\..\.."
set "JBC=%ROOT%\bin\jbc.exe"
set "VM=%ROOT%\sdk-dependiente\jasboot-ir\bin\jasboot-ir-vm-trace.exe"
set "TESTS_DIR=%~dp0"
set "OK=0"
set "FAIL=0"

if not exist "%JBC%" (echo [ERROR] jbc no encontrado. Ejecute sdk-dependiente\jas-compiler-c\build.bat & exit /b 1)
if not exist "%VM%" (echo [ERROR] VM no encontrada. Ejecute sdk-dependiente\jasboot-ir\build_vm.bat & exit /b 1)

echo === Tests Math 3D (trig, exp/log, mat4, mat3) ===

for %%F in (test_math3d_trig test_math3d_exp_log test_math3d_mat4 test_math3d_mat3) do (
    echo.
    echo [%%F.jasb]
    "%JBC%" "%TESTS_DIR%%%F.jasb" -o "%TESTS_DIR%%%F.jbo" 2>nul
    if errorlevel 1 (
        echo   Compilacion FALLO
        set /a FAIL+=1
    ) else (
        "%VM%" "%TESTS_DIR%%%F.jbo" 2>nul
        if errorlevel 1 (
            echo   Ejecucion FALLO
            set /a FAIL+=1
        ) else (
            echo   OK
            set /a OK+=1
        )
        del "%TESTS_DIR%%%F.jbo" 2>nul
    )
)

echo.
echo === Resumen: %OK% OK, %FAIL% Fallos ===
exit /b %FAIL%
