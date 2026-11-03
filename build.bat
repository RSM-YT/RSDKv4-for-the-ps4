SETLOCAL EnableDelayedExpansion

if not defined OO_PS4_TOOLCHAIN (
    echo ERROR: OO_PS4_TOOLCHAIN must point to your PS4 toolchain.
    exit /b 1
)
if not exist "%OO_PS4_TOOLCHAIN%\link.x" (
    echo ERROR: PS4 linker script not found: "%OO_PS4_TOOLCHAIN%\link.x"
    exit /b 1
)

Rem Package information
set PKG_TITLE="RetroEngine PS4"
set PKG_VERSION="1.00"
set PKG_ASSETS="assets"
set PKG_TITLE_ID="REPP00100"
set PKG_CONTENT_ID="IV0000-REPP00100_00-REPSSDKV40000000"

Rem Libraries to link in
set libraries=-lSDL2 -lScePigletv2VSH -lSceAudioOut -lSceVideoOut -lScePad -lSceUserService -lc -lkernel -lc++ -lSceSysmodule -lSceSystemService
set extra_flags=-I"%OO_PS4_TOOLCHAIN%\include\SDL2" -DRETRO_PS4_BUILD=1 -DRETRO_USE_NETWORKING=0 -O2 -ferror-limit=0 -Wno-unused-command-line-argument

Rem Read the script arguments into local vars
set "intdir=%~1"
set "targetname=%~2"
set "outputPath=%~3"

Rem Allow running this script directly without Visual Studio build arguments
if not defined intdir set "intdir=pleasw\x64\Debug"
if not defined targetname set "targetname=pleasw"

set "outputElf=%intdir%\%targetname%.elf"
set "outputOelf=%intdir%\%targetname%.oelf"
set sys_authinfo="000000000000000000000000001C004000FF000000000080000000000000000000000000000000000000008000400040000000000000008000000000000000080040FFFF000000F000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
if not exist "%intdir%" mkdir "%intdir%"
if errorlevel 1 exit /b 1

Rem Remove the obsolete direct-Piglet object from incremental builds
if exist "%intdir%\PigletGL.o" del /q "%intdir%\PigletGL.o"

Rem Compile object files for all the source files
for %%f in (*.c) do (
    clang --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -I"%CD%" -I"%OO_PS4_TOOLCHAIN%\include" -I"%OO_PS4_TOOLCHAIN%\include\c++\v1" %extra_flags% -c -o "%intdir%\%%~nf.o" "%%~nf.c"
    if errorlevel 1 exit /b 1
)

Rem Build the Ogg/Vorbis decoder from the sources shipped with the PS4 toolchain
for %%f in (bitwise.c framing.c) do (
    clang --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -I"%CD%" -I"%OO_PS4_TOOLCHAIN%\include" -I"%OO_PS4_TOOLCHAIN%\include\c++\v1" %extra_flags% -c -o "%intdir%\ogg_%%~nf.o" "%OO_PS4_TOOLCHAIN%\include\ogg\%%f"
    if errorlevel 1 exit /b 1
)

for %%f in (mdct.c smallft.c block.c envelope.c window.c lsp.c lpc.c analysis.c synthesis.c psy.c info.c floor1.c floor0.c res0.c mapping0.c registry.c codebook.c sharedbook.c lookup.c bitrate.c vorbisfile.c) do (
    clang --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -I"%CD%" -I"%OO_PS4_TOOLCHAIN%\include" -I"%OO_PS4_TOOLCHAIN%\include\c++\v1" %extra_flags% -c -o "%intdir%\vorbis_%%~nf.o" "%OO_PS4_TOOLCHAIN%\include\vorbis\%%f"
    if errorlevel 1 exit /b 1
)

for %%f in (*.cpp) do (
    clang++ --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -I"%CD%" -I"%OO_PS4_TOOLCHAIN%\include" -I"%OO_PS4_TOOLCHAIN%\include\c++\v1" %extra_flags% -c -o "%intdir%\%%~nf.o" "%%~nf.cpp"
    if errorlevel 1 exit /b 1
)

Rem Get a list of object files for linking
set obj_files=
for %%f in ("%intdir%\*.o") do set obj_files=!obj_files! "%%f"

Rem Link the input ELF
ld.lld -m elf_x86_64 -pie --script "%OO_PS4_TOOLCHAIN%\link.x" --eh-frame-hdr -o "%outputElf%" "-L%OO_PS4_TOOLCHAIN%\lib" %libraries% --verbose "%OO_PS4_TOOLCHAIN%\lib\crt1.o" %obj_files%
if errorlevel 1 exit /b 1

Rem Create the eboot
%OO_PS4_TOOLCHAIN%\bin\windows\create-fself.exe -in "%outputElf%" --out "%outputOelf%" --eboot "eboot.bin" --paid 0x3800000000000035 --authinfo %sys_authinfo%
if errorlevel 1 exit /b 1

Rem Create param.sfo
%OO_PS4_TOOLCHAIN%\bin\windows\PkgTool.Core.exe sfo_new sce_sys/param.sfo
if errorlevel 1 exit /b 1
%OO_PS4_TOOLCHAIN%\bin\windows\PkgTool.Core.exe sfo_setentry sce_sys/param.sfo APP_TYPE --type Integer --maxsize 4 --value 1
if errorlevel 1 exit /b 1
%OO_PS4_TOOLCHAIN%\bin\windows\PkgTool.Core.exe sfo_setentry sce_sys/param.sfo APP_VER --type Utf8 --maxsize 8 --value %PKG_VERSION%
if errorlevel 1 exit /b 1
%OO_PS4_TOOLCHAIN%\bin\windows\PkgTool.Core.exe sfo_setentry sce_sys/param.sfo ATTRIBUTE --type Integer --maxsize 18 --value 0
if errorlevel 1 exit /b 1
%OO_PS4_TOOLCHAIN%\bin\windows\PkgTool.Core.exe sfo_setentry sce_sys/param.sfo CATEGORY --type Utf8 --maxsize 4 --value "gde"
if errorlevel 1 exit /b 1
%OO_PS4_TOOLCHAIN%\bin\windows\PkgTool.Core.exe sfo_setentry sce_sys/param.sfo FORMAT --type Utf8 --maxsize 4 --value "obs"
if errorlevel 1 exit /b 1
%OO_PS4_TOOLCHAIN%\bin\windows\PkgTool.Core.exe sfo_setentry sce_sys/param.sfo CONTENT_ID --type Utf8 --maxsize 48 --value %PKG_CONTENT_ID%
if errorlevel 1 exit /b 1
%OO_PS4_TOOLCHAIN%\bin\windows\PkgTool.Core.exe sfo_setentry sce_sys/param.sfo DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
if errorlevel 1 exit /b 1
%OO_PS4_TOOLCHAIN%\bin\windows\PkgTool.Core.exe sfo_setentry sce_sys/param.sfo SYSTEM_VER --type Integer --maxsize 4 --value 0
if errorlevel 1 exit /b 1
%OO_PS4_TOOLCHAIN%\bin\windows\PkgTool.Core.exe sfo_setentry sce_sys/param.sfo TITLE --type Utf8 --maxsize 128 --value %PKG_TITLE%
if errorlevel 1 exit /b 1
%OO_PS4_TOOLCHAIN%\bin\windows\PkgTool.Core.exe sfo_setentry sce_sys/param.sfo TITLE_ID --type Utf8 --maxsize 12 --value %PKG_TITLE_ID%
if errorlevel 1 exit /b 1
%OO_PS4_TOOLCHAIN%\bin\windows\PkgTool.Core.exe sfo_setentry sce_sys/param.sfo VERSION --type Utf8 --maxsize 8 --value %PKG_VERSION%
if errorlevel 1 exit /b 1

Rem Get a list of assets for packaging
set module_files=
for %%f in (sce_module\\*) do set module_files=!module_files! sce_module/%%~nxf

set asset_audio_files=
for %%f in (assets\\audio\\*) do set asset_audio_files=!asset_audio_files! assets/audio/%%~nxf

set asset_fonts_files=
for %%f in (assets\\fonts\\*) do set asset_fonts_files=!asset_fonts_files! assets/fonts/%%~nxf

set asset_images_files=
for %%f in (assets\\images\\*) do set asset_images_files=!asset_images_files! assets/images/%%~nxf

set asset_misc_files=
for %%f in (assets\\misc\\*) do set asset_misc_files=!asset_misc_files! assets/misc/%%~nxf

set asset_videos_files=
for %%f in (assets\\videos\\*) do set asset_videos_files=!asset_videos_files! assets/videos/%%~nxf

Rem Create gp4
%OO_PS4_TOOLCHAIN%\bin\windows\create-gp4.exe -out pkg.gp4 --content-id=%PKG_CONTENT_ID% --files "eboot.bin sce_sys/about/right.sprx sce_sys/param.sfo sce_sys/icon0.png %module_files% %asset_audio_files% %asset_fonts_files% %asset_images_files% %asset_misc_files% %asset_videos_files%"
if errorlevel 1 exit /b 1

Rem Create pkg
%OO_PS4_TOOLCHAIN%\bin\windows\PkgTool.Core.exe pkg_build pkg.gp4 ..
if errorlevel 1 exit /b 1
