#ifdef __EMSCRIPTEN__

#include "MarioController.h"

#include <cmath>

extern "C" {
#include "z64.h"
#include "z64actor.h"
#include "macros.h"
}

SM64MarioInputs MarioController::BuildInputs(PlayState* play) {
    SM64MarioInputs inputs{};

    // --- Stick ---
    Input* ctrl = &play->state.input[0];
    float sx = (float)ctrl->cur.stick_x / 64.0f;  // N64 stick range: -64..+63
    float sy = (float)ctrl->cur.stick_y / 64.0f;

    // Clamp to unit circle
    float mag = sqrtf(sx * sx + sy * sy);
    if (mag > 1.0f) { sx /= mag; sy /= mag; }

    // SM64 uses camera-relative stick direction.
    // Extract the camera's yaw from OoT's active camera.
    // play->activeCamera indexes into play->cameraPtrs[]
    Camera* cam = GET_ACTIVE_CAM(play);

    // cam->eye and cam->at are Vec3f world-space positions.
    // The camera forward (horizontal) vector gives us the yaw.
    float dx = cam->at.x - cam->eye.x;
    float dz = cam->at.z - cam->eye.z;
    float camYaw = atan2f(dx, dz);   // angle of camera forward in XZ plane

    // Rotate stick to be camera-relative
    float cosY = cosf(-camYaw);
    float sinY = sinf(-camYaw);
    inputs.stickX =  sx * cosY + sy * sinY;
    inputs.stickY = -sx * sinY + sy * cosY;

    // Clamp to [-1, 1]
    if (inputs.stickX >  1.0f) inputs.stickX =  1.0f;
    if (inputs.stickX < -1.0f) inputs.stickX = -1.0f;
    if (inputs.stickY >  1.0f) inputs.stickY =  1.0f;
    if (inputs.stickY < -1.0f) inputs.stickY = -1.0f;

    // SM64 wants the camera look direction so it can compute relative movement.
    // Since we already rotated the stick above, pass zero here.
    inputs.camLookX = 0.0f;
    inputs.camLookZ = 0.0f;

    // --- Buttons ---
    uint16_t held = ctrl->cur.button;
    inputs.buttonA = (held & BTN_A) ? 1 : 0;  // Jump
    inputs.buttonB = (held & BTN_B) ? 1 : 0;  // Punch / kick
    inputs.buttonZ = (held & BTN_Z) ? 1 : 0;  // Crouch / ground-pound

    return inputs;
}

#endif // __EMSCRIPTEN__
