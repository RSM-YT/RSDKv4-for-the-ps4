# **RSDKv4 PS4 Port**
## Native port of the Retro Engine v4 (RSDKv4) to PlayStation 4 using OpenOrbis and Piglet (OpenGL ES 2.0).

### Features
- Full software renderer path with GLES presentation via Piglet
- Runs at 1920×1080 (internal resolution configurable)
- Support for Sonic 1 and Sonic 2.
- mod loading
- SDL2 for input and (partial) audio
- Works on firmware 13.52 with GoldHEN

### Building
- OpenOrbis PS4 Toolchain
- Requires `libScePigletv2VSH.sprx` and `libSceShaccVSH.sprx` (place in `sce_module/` or `/data/self/system/common/lib/`)
- Uses a custom authinfo for canvas/compositor access
- Uses a build.bat to build binaries

### **Setup**
- Needs a jailbroken ps4, your own copy of sonic 1/2's data.rsdk and GoldHen. Ps5 not tested.
- Run the application once. It will create the necessary folders. And crash.
- Then put your data.rsdk file into the newly created RSDKv4 folder in /data
- Add the Scripts folder to /data/RSDKv4. Ps4 is case sensitive so DO NOT CHANGE IT
- Run the application once. if it fails to load, reboot and run it 1-3 times
- If it still fails to load then create the directories: </data/self/system/common/lib> and drag both .sprx files in there
  
### Status
- Gameplay and rendering working
- Title screen and stages load
- Audio device selection now works
- Mod support.

### Credits
- https://github.com/RSDKModding/RSDKv4-Decompilation - rubber ducky for base code and other things

- https://github.com/openorbis - compilkation and other things

- https://claude.ai/ - For aiding in helping me understand Piglet and the ps4 and fixing up some code.

- 𝓗𝓪𝓻𝓾 (endou.haru.fr) - on discord for helping me fix memory issues.
- OpenOrbis toolchain
- flatz (Piglet / OpenGL ES on PS4)
- Everyone who documented the canvas authinfo and module loading requirements

---
Hope you enjoy! took 6 months to do.
