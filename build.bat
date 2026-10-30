@echo off
:: Build Loomy on Windows. Needs gcc + windres on PATH.
:: Easiest way: download w64devkit (https://github.com/skeeto/w64devkit), unzip, add its bin folder to PATH.
cd /d "%~dp0"
windres -i res\loomy.rc -o res.o -O coff -I res || goto :fail
gcc -Os -s -municode -ffreestanding -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables ^
    -fno-tree-loop-distribute-patterns -ffunction-sections -Wall -Wno-unused-parameter ^
    src\browser.c src\commands.c src\config.c src\net.c src\settings.c src\update.c src\changelog.c src\modes.c src\web.c src\display.c src\anim.c src\features.c src\sysinfo.c src\sysdata.c src\sysview.c src\version.c src\ui.c src\util.c res.o -o loomy.exe ^
    -nostdlib -nostartfiles -e entry -mwindows -Wl,--gc-sections,--subsystem,windows ^
    -lkernel32 -luser32 -lgdi32 -lshell32 -ladvapi32 -ldwmapi -liphlpapi -lws2_32 -lwinhttp -lgcc || goto :fail
del res.o
echo Built loomy.exe
exit /b 0
:fail
echo Build failed.
exit /b 1
