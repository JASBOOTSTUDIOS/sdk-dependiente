[Version]
Class=IExpress
SEDVersion=3
[Options]
PackagePurpose=InstallApp
ShowInstallProgramWindow=0
HideExtractAnimation=0
UseLongFileName=1
InsideCompressed=0
CAB_FixedSize=0
CAB_ResvCodeSigning=0
RebootMode=N
InstallPrompt=%InstallPrompt%
DisplayLicense=%DisplayLicense%
FinishMessage=%FinishMessage%
TargetName=%TargetName%
FriendlyName=%FriendlyName%
AppLaunched=%AppLaunched%
PostInstallCmd=%PostInstallCmd%
AdminQuietInstCmd=%AdminQuietInstCmd%
UserQuietInstCmd=%UserQuietInstCmd%
SourceFiles=SourceFiles
[Strings]
InstallPrompt=¿Desea instalar Jasboot SDK v0.0.1 en su equipo de manera global?
DisplayLicense=
FinishMessage=¡Jasboot SDK v0.0.1 ha sido configurado correctamente! Ahora puedes usar 'jbc' desde cualquier terminal.
TargetName=C:\src\jasboot\sdk-dependiente\distribuible\JasbootSetup-v0.0.1.exe
FriendlyName=Jasboot SDK v0.0.1 Installer
AppLaunched=cmd.exe /c instalar_jasboot.bat
PostInstallCmd=<None>
AdminQuietInstCmd=
UserQuietInstCmd=
[SourceFiles]
SourceFiles0=C:\src\jasboot\sdk-dependiente\distribuible\
SourceFiles1=C:\src\jasboot\sdk-dependiente\distribuible\bin\
SourceFiles2=C:\src\jasboot\sdk-dependiente\distribuible\img\
[SourceFiles0]
instalar_jasboot.bat=
[SourceFiles1]
jbc.exe=
jasboot-ir-vm.exe=
[SourceFiles2]
jasboot-icon.png=
jasboot-icon.ico=
