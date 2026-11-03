// 1. Force the standard C integer absolute value definition into scope first
#include <stdlib.h>
#include <orbis/libkernel.h>
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include "RetroEngine.hpp"
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include <cstdio>
#include <unistd.h>
#include <orbis/Pigletv2VSH.h>
#include <orbis/libkernel.h>
// 2. Explicitly provide a global fallback signature for the compiler
#ifdef __cplusplus
extern "C" {
int abs(int j);
}
#endif
#ifdef DECLSPEC
#undef DECLSPEC
#endif
#define DECLSPEC
#include <RetroEngine.hpp>
#if !RETRO_USE_ORIGINAL_CODE
#if RETRO_PLATFORM == RETRO_WIN
#include "Windows.h"
#endif
void RSDKv4(int i)
{
    printf("Function Stack: RSDKv4\n");
		mkdir("/data", 0777);
    
	mkdir(BASE_PATH, 0777);
    printf("Initializing RSDKv4 directory...\n");
	mkdir(BASE_PATH "mods", 0777);
    printf("Initializing Mods directory...\n");
    mkdir(BASE_PATH "Sonic1", 0777);
    printf("Initializing Sonic1 directory...\n");
	mkdir(BASE_PATH "Sonic2", 0777);
    printf("Initializing Sonic2 directory...\n");
    // Ensure required data files exist to avoid missing-file errors on startup
    // Create default settings.ini, controllerdb.txt, UData.bin and mods/modconfig.ini if absent
    {
        FILE *f = NULL;
        // settings.ini
        f = fopen(BASE_PATH "settings.ini", "r");
        if (!f) {
            f = fopen(BASE_PATH "settings.ini", "w");
            if (f) {
                fprintf(f, "[Dev]\nDevMenu=0\nEngineDebugMode=0\nTxtScripts=0\nStartingCategory=255\nStartingScene=255\nStartingPlayer=255\nStartingSaveFile=255\nFastForwardSpeed=8\n\n");
                fprintf(f, "[Game]\nLanguage=0\nGameType=0\nSkipStartMenu=0\nDisableFocusPause=0\n\n");
                fprintf(f, "[Window]\nFullScreen=0\nBorderless=0\nVSync=0\nScalingMode=0\nWindowScale=2\nScreenWidth=424\nRefreshRate=60\nDimLimit=300\n\n");
                fprintf(f, "[Audio]\nBGMVolume=1.0\nSFXVolume=1.0\n");
                fclose(f);
                printf("Created default " BASE_PATH "settings.ini\n");
            }
        }
        else
            fclose(f);

        // controllerdb.txt
        f = fopen(BASE_PATH "controllerdb.txt", "r");
        if (!f) {
            f = fopen(BASE_PATH "controllerdb.txt", "w");
            if (f) {
                // empty default
                fprintf(f, "# controller mappings\n");
                fclose(f);
                printf("Created default " BASE_PATH "controllerdb.txt\n");
            }
        }
        else
            fclose(f);

        // mods/modconfig.ini
        f = fopen(BASE_PATH "mods/modconfig.ini", "r");
        if (!f) {
            f = fopen(BASE_PATH "mods/modconfig.ini", "w");
            if (f) {
                fprintf(f, "; mod configuration file\n");
                fclose(f);
                printf("Created default " BASE_PATH "mods/modconfig.ini\n");
            }
        }
        else
            fclose(f);

        // UData.bin (user data) - create empty binary file if missing
        f = fopen(BASE_PATH "UData.bin", "rb");
        if (!f) {
            f = fopen(BASE_PATH "UData.bin", "wb");
            if (f) {
                unsigned char zero = 0;
                fwrite(&zero, 1, 1, f);
                fclose(f);
                printf("Created default " BASE_PATH "UData.bin\n");
            }
        }
        else
            fclose(f);
    }

    chdir(BASE_PATH);
    printf("Changed main directory to " BASE_PATH "\n");
}
void parseArguments(int argc, char *argv[])
{
    printf("Function Stack: parseArguments\n");
    for (int a = 0; a < argc; ++a) {
        const char *find = "";

        find = strstr(argv[a], "stage=");
        printf("Parsing stage argument...\n");
        if (find) {
            const char *value = find + 6;
            size_t length     = strcspn(value, ";");
            if (length >= sizeof(Engine.startSceneFolder)) {
                printf("Stage argument is too long; truncating it.\n");
                length = sizeof(Engine.startSceneFolder) - 1;
            }
            memcpy(Engine.startSceneFolder, value, length);
            Engine.startSceneFolder[length] = '\0';
            printf("Engine.startSceneFolder = %s\n", Engine.startSceneFolder);
        }
        else if (!find)
        {
            printf("Couldnt parse stage argument\n");

        }

        find = strstr(argv[a], "scene=");
        printf("Parsing scene argument...\n");
        if (find) {
            const char *value = find + 6;
            size_t length     = strcspn(value, ";");
            if (length >= sizeof(Engine.startSceneID)) {
                printf("Scene argument is too long; truncating it.\n");
                length = sizeof(Engine.startSceneID) - 1;
            }
            memcpy(Engine.startSceneID, value, length);
            Engine.startSceneID[length] = '\0';
            printf("Engine.startSceneID = %s\n", Engine.startSceneID);
        }
        else if (!find)
        {
            printf("Couldnt parse scene argument\n");

        }

        find = strstr(argv[a], "console=true");
        printf("Parsing console argument...\n");
        if (find) {
            engineDebugMode       = true;
            printf("EnginedebugMode = %s\n", engineDebugMode ? "true" : "false");
            Engine.devMenu        = true;
            printf("Engine.devMenu = %s\n", Engine.devMenu ? "true" : "false");
            Engine.consoleEnabled = true;
            printf("Engine.consoleEnabled = %s\n", Engine.consoleEnabled ? "true" : "false");
#if RETRO_PLATFORM == RETRO_WIN
            //AllocConsole();
            //freopen((FILE **)stdin, "CONIN$", "w", stdin);
            //freopen((FILE **)stdout, "CONOUT$", "w", stdout);
            //freopen((FILE **)stderr, "CONOUT$", "w", stderr);
#endif
        }
        else if (!find)
        {
            printf("Couldnt parse console argument\n");

        }

        find = strstr(argv[a], "usingCWD=true");
        printf("Finding usingCWD argument...\n");
        if (find) {
            printf("Found!\n");

            usingCWD = true;
            printf("usingCWD = %s\n", usingCWD ? "true" : "false");
            
        }
        else if (!find)
        {
            printf("Couldnt find usingCWD argument\n");

        }
    }
}
#endif

int main(int argc, char *argv[])
{
    SDL_SetAssertionHandler([](const SDL_AssertData*, void*) { return SDL_ASSERTION_IGNORE; }, NULL);
    printf("Function Stack: main\n");
#if !RETRO_USE_ORIGINAL_CODE


    printf("Running Function RSDKv4() line 139 col 5\n");
    RSDKv4(1);
    printf("Running Function parseArguments() line 137 col 5\n");
    parseArguments(argc, argv);



#endif
    Engine.Init();
    printf("Initializing Engine\n");
    if (!Engine.initialised) {
        fprintf(stderr, "ERROR: engine initialization failed; aborting startup.\n");
        return 1;
    }
    Engine.Run();
    printf("Running Engine\n");
    return 0;
}

#if RETRO_PLATFORM == RETRO_UWP
int __stdcall wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) { return SDL_WinRTRunApp(main, NULL); }
#endif
