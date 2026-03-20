#pragma once
#ifdef __EMSCRIPTEN__

#include <vector>
#include <stdint.h>
#include <libsm64.h>

// Forward declarations — CollisionHeader is fully defined in z64bgcheck.h
// which is included only in MarioCollision.cpp to avoid typedef conflicts.
typedef struct PlayState PlayState;

class MarioCollision {
public:
    // Build the full set of SM64 surfaces from OoT scene collision
    static std::vector<SM64Surface> BuildSurfacesFromScene(PlayState* play);

    // Dynamic surface objects (moving platforms etc.)
    // objectId is the SM64 surface object handle returned by sm64_surface_object_create
    static void UpdateDynamicObject(uint32_t objId,
                                    float x, float y, float z, float ry);
    static void DeleteDynamicObject(uint32_t objId);
};

#endif // __EMSCRIPTEN__
