# Counter Timer for OBS Studio

Native OBS source plugin for an adjustable count-up stopwatch and countdown, styled after the supplied clock reference. The Windows CI workflow builds a 64-bit DLL and bundles the plugin data folder into a ZIP artifact.

## Download the Windows build

After the first successful GitHub Actions run, open **Actions → Build Windows DLL → Artifacts** and download `counter-timer-windows-x64`. Extract the ZIP and follow `INSTALL.txt`.

## Build locally

This is an OBS plugin project. It requires the OBS libobs development SDK and FreeType. See the CMake project and the workflow for the Windows build configuration.

## Features

- Count-up and countdown modes with configurable initial hours, minutes and seconds.
- Start/pause and reset controls in the OBS source properties.
- Editable title, above/below/automatic placement, time format, font, sizing, tracking and line gap.
- Configurable colors, shadow and outline.

