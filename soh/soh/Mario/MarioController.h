#pragma once
#ifdef __EMSCRIPTEN__

#include <libsm64.h>

typedef struct PlayState PlayState;

class MarioController {
public:
    // Build SM64 inputs from OoT controller state + camera orientation
    static SM64MarioInputs BuildInputs(PlayState* play);
};

#endif // __EMSCRIPTEN__
