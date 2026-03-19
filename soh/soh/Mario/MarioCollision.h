#pragma once
#ifdef __EMSCRIPTEN__

#include <vector>
#include <libsm64.h>

typedef struct PlayState PlayState;
typedef struct CollisionHeader CollisionHeader;

class MarioCollision {
public:
    // Build the full set of SM64 surfaces from OoT scene collision
    static std::vector<SM64Surface> BuildSurfacesFromScene(PlayState* play);

    // Dynamic surface objects (moving platforms etc.)
    static uint32_t CreateDynamicObject(CollisionHeader* header,
                                        float x, float y, float z, float ry);
    static void UpdateDynamicObject(uint32_t objId,
                                    float x, float y, float z, float ry);
    static void DeleteDynamicObject(uint32_t objId);

private:
    static void AppendFromHeader(std::vector<SM64Surface>& out,
                                 CollisionHeader* header);
    static uint16_t MapSurfaceType(uint32_t ootData);
};

#endif // __EMSCRIPTEN__
