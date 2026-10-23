# Counter Timer — OBS Studio plugin

Windows x64 OBS source plugin inspired by the provided clock layout: large bold time, a smaller editable title, tight spacing, and an offset shadow.

## Features
- Count-up stopwatch and countdown with configurable initial hours/minutes/seconds.
- Start/Pause and Reset buttons in source properties; countdown can stop at zero.
- Time formats: HH:MM:SS, total MM:SS, or HH:MM:SS.hh.
- Editable title and visibility, title above/below/automatic placement, and left/center/right alignment.
- Adjustable font family, timer/title sizes, letter spacing, and line gap.
- Editable timer/title/shadow/outline colors, shadow offsets, and optional outline.

## Build and install
The Build Windows DLL workflow uses the official OBS plugin template's Windows dependency bootstrap, builds a 64-bit DLL, and publishes a ZIP artifact. Open GitHub Actions, select the latest successful Build Windows DLL run, and download counter-timer-windows-x64.

Extract the included counter-timer folder to %APPDATA%\obs-studio\plugins\ (create plugins if needed), or %PROGRAMDATA%\obs-studio\plugins\ for all users, then restart OBS.

Packaged layout:
- counter-timer/bin/64bit/counter-timer.dll
- counter-timer/data/locale/en-US.ini
