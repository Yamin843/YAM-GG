# YAM-GG

Injection-ready ImGui menu with JS console for Android apps.

## Build

Builds automatically via GitHub Actions.

## Features

- Independent GLSurfaceView (no EGL hook, no InputConsumer hook)
- Touch via standard View system
- JS console for script injection
- File browser for JS scripts from sdcard
- Draggable, resizable ImGui window
- Black glossy theme with gold borders

## Architecture

- Small dex (~30KB) with `ModView extends GLSurfaceView`
- Native `.so` embeds the dex in `.rodata`
- `JNI_OnLoad` loads dex via `InMemoryDexClassLoader`
- ImGui + OpenGL3 backend renders on its own render thread
- Frida-modified library (YAM-G) provides Java bridge for hooks

## Output

- `libYAMGG.so` (arm64-v8a) — inject into any app
