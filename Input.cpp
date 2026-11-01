#include <RetroEngine.hpp>

InputData keyPress = InputData();
InputData keyDown  = InputData();
int touchDown[8];
int touchX[8];
int touchY[8];
int touchID[8];
float touchXF[8];
float touchYF[8];
int touches = 0;

int hapticEffectNum = -2;

#if !RETRO_USE_ORIGINAL_CODE
#include <algorithm>
#include <vector>

InputButton inputDevice[INPUT_BUTTONCOUNT];
int inputType = 0;

// mania deadzone vals lol
float LSTICK_DEADZONE   = 0.3;
float RSTICK_DEADZONE   = 0.3;
float LTRIGGER_DEADZONE = 0.3;
float RTRIGGER_DEADZONE = 0.3;

int mouseHideTimer = 0;
int lastMouseX     = 0;
int lastMouseY     = 0;

struct InputDevice {
#if RETRO_USING_SDL2
    // we need the controller index reported from SDL2's controller added event
    int index;
    SDL_GameController *devicePtr;
    SDL_Joystick *joystickPtr;
    SDL_Haptic *hapticPtr;
#endif
#if RETRO_USING_SDL1
    SDL_Joystick *devicePtr;
#endif
    int id;
};

std::vector<InputDevice> controllers;

#if RETRO_USING_SDL1
byte keyState[SDLK_LAST];
#endif

#define normalize(val, minVal, maxVal) ((float)(val) - (float)(minVal)) / ((float)(maxVal) - (float)(minVal))

#if RETRO_USING_SDL2
bool getControllerButton(byte buttonID)
{
    bool pressed = false;

    for (int i = 0; i < controllers.size(); ++i) {
        SDL_GameController *controller = controllers[i].devicePtr;

        if (!controller) {
            SDL_Joystick *joystick = controllers[i].joystickPtr;
            if (!joystick)
                continue;

            if (buttonID == SDL_CONTROLLER_BUTTON_TOUCHPAD) {
                pressed |= SDL_JoystickGetButton(joystick, 13);
                continue;
            }

            auto axisValue = [joystick](int axis) {
                if (axis < 0 || axis >= SDL_JoystickNumAxes(joystick))
                    return 0.0f;
                return SDL_JoystickGetAxis(joystick, axis) / 32767.0f;
            };
            auto axisDirection = [&axisValue](int axis, bool negative, float deadzone) {
                float value = axisValue(axis);
                return negative ? value < -deadzone : value > deadzone;
            };

            Uint8 hat = SDL_JoystickNumHats(joystick) > 0 ? SDL_JoystickGetHat(joystick, 0) : SDL_HAT_CENTERED;
            switch (buttonID) {
                case SDL_CONTROLLER_BUTTON_A: pressed |= SDL_JoystickGetButton(joystick, 1); continue;
                case SDL_CONTROLLER_BUTTON_B: pressed |= SDL_JoystickGetButton(joystick, 2); continue;
                case SDL_CONTROLLER_BUTTON_X: pressed |= SDL_JoystickGetButton(joystick, 0); continue;
                case SDL_CONTROLLER_BUTTON_Y: pressed |= SDL_JoystickGetButton(joystick, 3); continue;
                case SDL_CONTROLLER_BUTTON_BACK: pressed |= SDL_JoystickGetButton(joystick, 8); continue;
                case SDL_CONTROLLER_BUTTON_GUIDE: pressed |= SDL_JoystickGetButton(joystick, 12); continue;
                case SDL_CONTROLLER_BUTTON_START: pressed |= SDL_JoystickGetButton(joystick, 9); continue;
                case SDL_CONTROLLER_BUTTON_LEFTSTICK: pressed |= SDL_JoystickGetButton(joystick, 10); continue;
                case SDL_CONTROLLER_BUTTON_RIGHTSTICK: pressed |= SDL_JoystickGetButton(joystick, 11); continue;
                case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: pressed |= SDL_JoystickGetButton(joystick, 4); continue;
                case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: pressed |= SDL_JoystickGetButton(joystick, 5); continue;
                case SDL_CONTROLLER_BUTTON_DPAD_UP:
                    pressed |= (hat & SDL_HAT_UP) || axisDirection(1, true, LSTICK_DEADZONE);
                    continue;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                    pressed |= (hat & SDL_HAT_DOWN) || axisDirection(1, false, LSTICK_DEADZONE);
                    continue;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                    pressed |= (hat & SDL_HAT_LEFT) || axisDirection(0, true, LSTICK_DEADZONE);
                    continue;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                    pressed |= (hat & SDL_HAT_RIGHT) || axisDirection(0, false, LSTICK_DEADZONE);
                    continue;
                case SDL_CONTROLLER_BUTTON_ZL: pressed |= axisValue(4) > LTRIGGER_DEADZONE; continue;
                case SDL_CONTROLLER_BUTTON_ZR: pressed |= axisValue(5) > RTRIGGER_DEADZONE; continue;
                case SDL_CONTROLLER_BUTTON_LSTICK_UP: pressed |= axisDirection(1, true, LSTICK_DEADZONE); continue;
                case SDL_CONTROLLER_BUTTON_LSTICK_DOWN: pressed |= axisDirection(1, false, LSTICK_DEADZONE); continue;
                case SDL_CONTROLLER_BUTTON_LSTICK_LEFT: pressed |= axisDirection(0, true, LSTICK_DEADZONE); continue;
                case SDL_CONTROLLER_BUTTON_LSTICK_RIGHT: pressed |= axisDirection(0, false, LSTICK_DEADZONE); continue;
                case SDL_CONTROLLER_BUTTON_RSTICK_UP: pressed |= axisDirection(3, true, RSTICK_DEADZONE); continue;
                case SDL_CONTROLLER_BUTTON_RSTICK_DOWN: pressed |= axisDirection(3, false, RSTICK_DEADZONE); continue;
                case SDL_CONTROLLER_BUTTON_RSTICK_LEFT: pressed |= axisDirection(2, true, RSTICK_DEADZONE); continue;
                case SDL_CONTROLLER_BUTTON_RSTICK_RIGHT: pressed |= axisDirection(2, false, RSTICK_DEADZONE); continue;
                default: continue;
            }
        }

        if (buttonID == SDL_CONTROLLER_BUTTON_TOUCHPAD) {
            SDL_Joystick *joystick = SDL_GameControllerGetJoystick(controller);
            pressed |= joystick && SDL_JoystickGetButton(joystick, 13);
            continue;
        }

        if (SDL_GameControllerGetButton(controller, (SDL_GameControllerButton)buttonID)) {
            pressed |= true;
            continue;
        }
        else {
            switch (buttonID) {
                default: break;
                case SDL_CONTROLLER_BUTTON_DPAD_UP: {
                    int axis    = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY);
                    float delta = 0;
                    if (axis < 0)
                        delta = -normalize(-axis, 1, 32768);
                    else
                        delta = normalize(axis, 0, 32767);
                    pressed |= delta < -LSTICK_DEADZONE;
                    continue;
                }
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN: {
                    int axis    = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY);
                    float delta = 0;
                    if (axis < 0)
                        delta = -normalize(-axis, 1, 32768);
                    else
                        delta = normalize(axis, 0, 32767);
                    pressed |= delta > LSTICK_DEADZONE;
                    continue;
                }
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT: {
                    int axis    = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
                    float delta = 0;
                    if (axis < 0)
                        delta = -normalize(-axis, 1, 32768);
                    else
                        delta = normalize(axis, 0, 32767);
                    pressed |= delta < -LSTICK_DEADZONE;
                    continue;
                }
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: {
                    int axis    = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
                    float delta = 0;
                    if (axis < 0)
                        delta = -normalize(-axis, 1, 32768);
                    else
                        delta = normalize(axis, 0, 32767);
                    pressed |= delta > LSTICK_DEADZONE;
                    continue;
                }
            }
        }

        switch (buttonID) {
            default: break;
            case SDL_CONTROLLER_BUTTON_ZL: {
                float delta = normalize(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT), 0, 32767);
                pressed |= delta > LTRIGGER_DEADZONE;
                continue;
            }
            case SDL_CONTROLLER_BUTTON_ZR: {
                float delta = normalize(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT), 0, 32767);
                pressed |= delta > RTRIGGER_DEADZONE;
                continue;
            }
            case SDL_CONTROLLER_BUTTON_LSTICK_UP: {
                int axis    = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY);
                float delta = 0;
                if (axis < 0)
                    delta = -normalize(-axis, 1, 32768);
                else
                    delta = normalize(axis, 0, 32767);
                pressed |= delta < -LSTICK_DEADZONE;
                continue;
            }
            case SDL_CONTROLLER_BUTTON_LSTICK_DOWN: {
                int axis    = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY);
                float delta = 0;
                if (axis < 0)
                    delta = -normalize(-axis, 1, 32768);
                else
                    delta = normalize(axis, 0, 32767);
                pressed |= delta > LSTICK_DEADZONE;
                continue;
            }
            case SDL_CONTROLLER_BUTTON_LSTICK_LEFT: {
                int axis    = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
                float delta = 0;
                if (axis < 0)
                    delta = -normalize(-axis, 1, 32768);
                else
                    delta = normalize(axis, 0, 32767);
                pressed |= delta > LSTICK_DEADZONE;
                continue;
            }
            case SDL_CONTROLLER_BUTTON_LSTICK_RIGHT: {
                int axis    = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
                float delta = 0;
                if (axis < 0)
                    delta = -normalize(-axis, 1, 32768);
                else
                    delta = normalize(axis, 0, 32767);
                pressed |= delta < -LSTICK_DEADZONE;
                continue;
            }
            case SDL_CONTROLLER_BUTTON_RSTICK_UP: {
                int axis    = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY);
                float delta = 0;
                if (axis < 0)
                    delta = -normalize(-axis, 1, 32768);
                else
                    delta = normalize(axis, 0, 32767);
                pressed |= delta < -RSTICK_DEADZONE;
                continue;
            }
            case SDL_CONTROLLER_BUTTON_RSTICK_DOWN: {
                int axis    = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY);
                float delta = 0;
                if (axis < 0)
                    delta = -normalize(-axis, 1, 32768);
                else
                    delta = normalize(axis, 0, 32767);
                pressed |= delta > RSTICK_DEADZONE;
                continue;
            }
            case SDL_CONTROLLER_BUTTON_RSTICK_LEFT: {
                int axis    = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTX);
                float delta = 0;
                if (axis < 0)
                    delta = -normalize(-axis, 1, 32768);
                else
                    delta = normalize(axis, 0, 32767);
                pressed |= delta > RSTICK_DEADZONE;
                continue;
            }
            case SDL_CONTROLLER_BUTTON_RSTICK_RIGHT: {
                int axis    = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTX);
                float delta = 0;
                if (axis < 0)
                    delta = -normalize(-axis, 1, 32768);
                else
                    delta = normalize(axis, 0, 32767);
                pressed |= delta < -RSTICK_DEADZONE;
                continue;
            }
        }
    }

    return pressed;
}
#endif //! RETRO_USING_SDL2

void controllerInit(int controllerID)
{
    for (int i = 0; i < controllers.size(); ++i) {
        if (controllers[i].id == controllerID) {
            return; // we already opened this one!
        }
    }

#if RETRO_USING_SDL2
    SDL_GameController *controller = SDL_GameControllerOpen(controllerID);
    if (controller) {
        InputDevice device = {};
        device.id        = controllerID;
        device.index     = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller));
        device.devicePtr = controller;
        device.joystickPtr = SDL_GameControllerGetJoystick(controller);
#if RETRO_USE_HAPTICS
        device.hapticPtr = SDL_HapticOpenFromJoystick(SDL_GameControllerGetJoystick(controller));
        if (device.hapticPtr == NULL) {
            PrintLog("Could not open controller haptics...\nSDL_GetError() -> %s", SDL_GetError());
        }
        else {
            if (SDL_HapticRumbleInit(device.hapticPtr) < 0) {
                printf("Unable to initialize rumble!\nSDL_GetError() -> %s", SDL_GetError());
            }
        }
#endif

        controllers.push_back(device);
        inputType = 1;
    }
    else {
        SDL_Joystick *joystick = SDL_JoystickOpen(controllerID);
        if (!joystick) {
            PrintLog("Could not open controller %d: %s", controllerID, SDL_GetError());
            return;
        }

        InputDevice device = {};
        device.id = controllerID;
        device.index = SDL_JoystickInstanceID(joystick);
        device.joystickPtr = joystick;
        controllers.push_back(device);
        inputType = 1;
        PrintLog("Opened raw joystick %d: %s", controllerID, SDL_JoystickName(joystick));
    }
#endif
}

void controllerClose(int controllerID)
{
#if RETRO_USING_SDL2
    for (int i = 0; i < controllers.size(); ++i) {
        if (controllers[i].index == controllerID) {
            if (controllers[i].devicePtr)
                SDL_GameControllerClose(controllers[i].devicePtr);
            else if (controllers[i].joystickPtr)
                SDL_JoystickClose(controllers[i].joystickPtr);
#if RETRO_USING_SDL2
#if RETRO_USE_HAPTICS
            if (controllers[i].hapticPtr)
                SDL_HapticClose(controllers[i].hapticPtr);
#endif
#endif
            controllers.erase(controllers.begin() + i);
            break;
        }
    }
#endif

    if (controllers.empty())
        inputType = 0;
}

void InitInputDevices()
{
#if RETRO_USING_SDL2
    PrintLog("Initializing gamepads...");

    if (SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
        PrintLog("ERROR: failed to initialize SDL gamepad input: %s", SDL_GetError());
        return;
    }

    int joystickCount = SDL_NumJoysticks();
    controllers.clear();
    int gamepadCount = 0;

    for (int i = 0; i < joystickCount; i++) {
        if (!SDL_IsGameController(i)) {
            SDL_JoystickGUID guid = SDL_JoystickGetDeviceGUID(i);
            char guidString[33] = {};
            const char *name = SDL_JoystickNameForIndex(i);
            if (!name)
                name = "Unknown joystick";
            SDL_JoystickGetGUIDString(guid, guidString, sizeof(guidString));
            SDL_Joystick *joystick = SDL_JoystickOpen(i);
            if (joystick) {
                PrintLog("Joystick %d is not mapped as a gamepad: %s (GUID %s, %d buttons, %d axes, %d hats)",
                         i, name, guidString, SDL_JoystickNumButtons(joystick),
                         SDL_JoystickNumAxes(joystick), SDL_JoystickNumHats(joystick));
                InputDevice device = {};
                device.id = i;
                device.index = SDL_JoystickInstanceID(joystick);
                device.joystickPtr = joystick;
                controllers.push_back(device);
                ++gamepadCount;
            }
            else {
                PrintLog("Joystick %d is not mapped as a gamepad: %s (GUID %s): %s",
                         i, name, guidString, SDL_GetError());
            }
            continue;
        }

        SDL_GameController *gamepad = SDL_GameControllerOpen(i);
        if (!gamepad) {
            PrintLog("Could not open gamepad %d: %s", i, SDL_GetError());
            continue;
        }

        SDL_Joystick *joystick = SDL_GameControllerGetJoystick(gamepad);
        if (!joystick || !SDL_GameControllerGetAttached(gamepad)) {
            PrintLog("Gamepad %d detached during initialization.", i);
            SDL_GameControllerClose(gamepad);
            continue;
        }

        InputDevice device = {};
        device.id        = i;
        device.index     = SDL_JoystickInstanceID(joystick);
        device.devicePtr = gamepad;
        device.joystickPtr = joystick;
        controllers.push_back(device);

        PrintLog("Opened gamepad %d: %s", i, SDL_GameControllerName(gamepad));
        ++gamepadCount;
    }

    PrintLog("Found %d usable controllers among %d joysticks.", gamepadCount, joystickCount);
    SDL_GameControllerEventState(SDL_ENABLE);
#endif
}

void ReleaseInputDevices()
{
    for (int i = 0; i < controllers.size(); i++) {
#if RETRO_USING_SDL2
        if (controllers[i].devicePtr)
            SDL_GameControllerClose(controllers[i].devicePtr);
        else if (controllers[i].joystickPtr)
            SDL_JoystickClose(controllers[i].joystickPtr);
#if RETRO_USE_HAPTICS
        if (controllers[i].hapticPtr)
            SDL_HapticClose(controllers[i].hapticPtr);
#endif
#endif
    }
    controllers.clear();
}

void ProcessInput()
{
#if RETRO_USING_SDL2
    int length           = 0;
    const byte *keyState = SDL_GetKeyboardState(&length);

    if (inputType == 0) {
        for (int i = 0; i < INPUT_ANY; i++) {
            if (keyState[inputDevice[i].keyMappings]) {
                inputDevice[i].setHeld();
                if (!inputDevice[INPUT_ANY].hold)
                    inputDevice[INPUT_ANY].setHeld();
            }
            else if (inputDevice[i].hold)
                inputDevice[i].setReleased();
        }
    }
    else if (inputType == 1) {
        for (int i = 0; i < INPUT_ANY; i++) {
            if (getControllerButton(inputDevice[i].contMappings)) {
                inputDevice[i].setHeld();
                if (!inputDevice[INPUT_ANY].hold)
                    inputDevice[INPUT_ANY].setHeld();
            }
            else if (inputDevice[i].hold)
                inputDevice[i].setReleased();
        }
    }

    bool isPressed = false;
    for (int i = 0; i < INPUT_BUTTONCOUNT; i++) {
        if (keyState[inputDevice[i].keyMappings]) {
            isPressed = true;
            break;
        }
    }
    if (isPressed)
        inputType = 0;
    else if (inputType == 0)
        inputDevice[INPUT_ANY].setReleased();

    isPressed = false;
    for (int i = 0; i < INPUT_ANY; i++) {
        if (getControllerButton(inputDevice[i].contMappings)) {
            isPressed = true;
            break;
        }
    }
    if (isPressed)
        inputType = 1;
    else if (inputType == 1)
        inputDevice[INPUT_ANY].setReleased();

#if RETRO_PLATFORM == RETRO_PS4
    static bool previousDevMenuButton = false;
    static bool previousRestartButton = false;
    bool devMenuButton = getControllerButton(SDL_CONTROLLER_BUTTON_RIGHTSTICK);
    bool restartButton = getControllerButton(SDL_CONTROLLER_BUTTON_LEFTSTICK);

    if (Engine.devMenu && devMenuButton && !previousDevMenuButton) {
        ClearNativeObjects();
        CREATE_ENTITY(RetroGameLoop);
        Engine.gameMode = ENGINE_INITDEVMENU;
    }

    if (restartButton && !previousRestartButton
        && (Engine.gameMode == ENGINE_MAINGAME || Engine.gameMode == ENGINE_INITPAUSE || Engine.gameMode == ENGINE_EXITPAUSE)) {
        InitStartingStage(activeStageList, stageListPosition, playerListPos);
    }

    previousDevMenuButton = devMenuButton;
    previousRestartButton = restartButton;
#endif

    if (inputDevice[INPUT_ANY].press || inputDevice[INPUT_ANY].hold || touches > 1) {
        Engine.dimTimer = 0;
    }
    else if (Engine.dimTimer < Engine.dimLimit && !Engine.masterPaused) {
        ++Engine.dimTimer;
    }

#ifdef RETRO_USING_MOUSE
    if (touches <= 0) { // Touch always takes priority over mouse
        int mx = 0, my = 0;
        SDL_GetMouseState(&mx, &my);

        if (mx == lastMouseX && my == lastMouseY) {
            ++mouseHideTimer;
            if (mouseHideTimer == 120) {
                SDL_ShowCursor(false);
            }
        }
        else {
            if (mouseHideTimer >= 120)
                SDL_ShowCursor(true);
            mouseHideTimer  = 0;
            Engine.dimTimer = 0;
        }

        lastMouseX = mx;
        lastMouseY = my;
    }
#endif //! RETRO_USING_MOUSE

#elif RETRO_USING_SDL1
    if (SDL_NumJoysticks() > 0) {
        controller = SDL_JoystickOpen(0);

        // There's a problem opening the joystick
        if (controller == NULL) {
            // Uh oh
        }
        else {
            inputType = 1;
        }
    }
    else {
        if (controller) {
            // Close the joystick
            SDL_JoystickClose(controller);
        }
        controller = nullptr;
        inputType  = 0;
    }

    if (inputType == 0) {
        for (int i = 0; i < INPUT_BUTTONCOUNT; i++) {
            if (keyState[inputDevice[i].keyMappings]) {
                inputDevice[i].setHeld();
                inputDevice[INPUT_ANY].setHeld();
                continue;
            }
            else if (inputDevice[i].hold)
                inputDevice[i].setReleased();
        }
    }
    else if (inputType == 1 && controller) {
        for (int i = 0; i < INPUT_BUTTONCOUNT; i++) {
            if (SDL_JoystickGetButton(controller, inputDevice[i].contMappings)) {
                inputDevice[i].setHeld();
                inputDevice[INPUT_ANY].setHeld();
                continue;
            }
            else if (inputDevice[i].hold)
                inputDevice[i].setReleased();
        }
    }

    bool isPressed = false;
    for (int i = 0; i < INPUT_BUTTONCOUNT; i++) {
        if (keyState[inputDevice[i].keyMappings]) {
            isPressed = true;
            break;
        }
    }
    if (isPressed)
        inputType = 0;
    else if (inputType == 0)
        inputDevice[INPUT_ANY].setReleased();

    int buttonCnt = 0;
    if (controller)
        buttonCnt = SDL_JoystickNumButtons(controller);
    bool flag = false;
    for (int i = 0; i < buttonCnt; ++i) {
        flag      = true;
        inputType = 1;
    }
    if (!flag && inputType == 1) {
        inputDevice[INPUT_ANY].setReleased();
    }
#endif //! RETRO_USING_SDL2
}
#endif //! !RETRO_USE_ORIGINAL_CODE

// Pretty much is this code in the original, just formatted differently
void CheckKeyPress(InputData *input)
{
#if !RETRO_USE_ORIGINAL_CODE
    input->up     = inputDevice[INPUT_UP].press;
    input->down   = inputDevice[INPUT_DOWN].press;
    input->left   = inputDevice[INPUT_LEFT].press;
    input->right  = inputDevice[INPUT_RIGHT].press;
    input->A      = inputDevice[INPUT_BUTTONA].press;
    input->B      = inputDevice[INPUT_BUTTONB].press;
    input->C      = inputDevice[INPUT_BUTTONC].press;
    input->X      = inputDevice[INPUT_BUTTONX].press;
    input->Y      = inputDevice[INPUT_BUTTONY].press;
    input->Z      = inputDevice[INPUT_BUTTONZ].press;
    input->L      = inputDevice[INPUT_BUTTONL].press;
    input->R      = inputDevice[INPUT_BUTTONR].press;
    input->start  = inputDevice[INPUT_START].press;
    input->select = inputDevice[INPUT_SELECT].press;
#endif

#if RETRO_REV03
    SetGlobalVariableByName("input.pressButton", input->A || input->B || input->C || input->X || input->Y || input->Z || input->L || input->R
                                                     || input->start || input->select);
#endif
}

void CheckKeyDown(InputData *input)
{
#if !RETRO_USE_ORIGINAL_CODE
    input->up     = inputDevice[INPUT_UP].hold;
    input->down   = inputDevice[INPUT_DOWN].hold;
    input->left   = inputDevice[INPUT_LEFT].hold;
    input->right  = inputDevice[INPUT_RIGHT].hold;
    input->A      = inputDevice[INPUT_BUTTONA].hold;
    input->B      = inputDevice[INPUT_BUTTONB].hold;
    input->C      = inputDevice[INPUT_BUTTONC].hold;
    input->X      = inputDevice[INPUT_BUTTONX].hold;
    input->Y      = inputDevice[INPUT_BUTTONY].hold;
    input->Z      = inputDevice[INPUT_BUTTONZ].hold;
    input->L      = inputDevice[INPUT_BUTTONL].hold;
    input->R      = inputDevice[INPUT_BUTTONR].hold;
    input->start  = inputDevice[INPUT_START].hold;
    input->select = inputDevice[INPUT_SELECT].hold;
#endif
}

int CheckTouchRect(float x, float y, float w, float h)
{
    for (int f = 0; f < touches; ++f) {
        if (touchDown[f] && touchXF[f] > (x - w) && touchYF[f] > (y - h) && touchXF[f] <= (x + w) && touchYF[f] <= (y + h)) {
            return f;
        }
    }
    return -1;
}

int CheckTouchRectMatrix(void *m, float x, float y, float w, float h)
{
    MatrixF *mat = (MatrixF *)m;
    for (int f = 0; f < touches; ++f) {
        float tx = touchXF[f];
        float ty = touchYF[f];
        if (touchDown[f]) {
            float posX = (((tx * mat->values[0][0]) + (ty * mat->values[1][0])) + (mat->values[2][0] * SCREEN_YSIZE)) + mat->values[3][0];
            if (posX > (x - w) && posX <= (x + w)) {
                float posY = (((tx * mat->values[0][1]) + (ty * mat->values[1][1])) + (mat->values[2][1] * SCREEN_YSIZE)) + mat->values[3][1];
                if (posY > (y - h) && posY <= (y + h))
                    return f;
            }
        }
    }
    return -1;
}

#if RETRO_USE_HAPTICS
void HapticEffect(int *hapticID, int *a2, int *a3, int *a4)
{
    if (Engine.hapticsEnabled)
        hapticEffectNum = *hapticID;
}
#endif