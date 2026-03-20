#ifdef __EMSCRIPTEN__

#include "MarioCollision.h"

#include <stdio.h>
#include <vector>
#include <stdint.h>

// macros.h includes C++ headers (endianness.h), must be outside extern "C"
#include "macros.h"

// Pure C OoT headers — safe inside extern "C"
extern "C" {
#include "z64.h"
#include "z64bgcheck.h"
}

// ---------------------------------------------------------------------------
// Surface type mapping: OoT surface material → SM64 terrain/surface type
// ---------------------------------------------------------------------------

// OoT surface material values (lower byte of SurfaceType.data[0])
enum {
    OOT_MAT_NORMAL   = 0x00,
    OOT_MAT_SAND     = 0x02,
    OOT_MAT_STONE    = 0x03,
    OOT_MAT_LAVA     = 0x05,
    OOT_MAT_SHALLOW  = 0x09,
    OOT_MAT_ICE      = 0x0B,
    OOT_MAT_SNOW     = 0x0C,
    OOT_MAT_WATER    = 0x0F,
};

// SM64 surface/terrain constants (from decomp include/surface_terrains.h)
enum {
    SM64_SURFACE_DEFAULT       = 0x0000,
    SM64_SURFACE_BURNING       = 0x0001,
    SM64_SURFACE_WATER         = 0x000D,
    SM64_SURFACE_SHALLOW_WATER = 0x0029,
    SM64_SURFACE_ICE           = 0x00B0,

    SM64_TERRAIN_GRASS  = 0x0000,
    SM64_TERRAIN_STONE  = 0x0001,
    SM64_TERRAIN_SNOW   = 0x0002,
    SM64_TERRAIN_SAND   = 0x0003,
    SM64_TERRAIN_WATER  = 0x0005,
};

static uint16_t OotMatToSM64Terrain(uint8_t mat) {
    switch (mat) {
        case OOT_MAT_SAND:  return SM64_TERRAIN_SAND;
        case OOT_MAT_STONE: return SM64_TERRAIN_STONE;
        case OOT_MAT_SNOW:  return SM64_TERRAIN_SNOW;
        case OOT_MAT_SHALLOW:
        case OOT_MAT_WATER: return SM64_TERRAIN_WATER;
        default:            return SM64_TERRAIN_GRASS;
    }
}

static int16_t OotMatToSM64Surface(uint8_t mat) {
    switch (mat) {
        case OOT_MAT_LAVA:    return SM64_SURFACE_BURNING;
        case OOT_MAT_ICE:     return SM64_SURFACE_ICE;
        case OOT_MAT_SHALLOW: return SM64_SURFACE_SHALLOW_WATER;
        case OOT_MAT_WATER:   return SM64_SURFACE_WATER;
        default:              return SM64_SURFACE_DEFAULT;
    }
}

// ---------------------------------------------------------------------------
// Internal helper — not declared in header to avoid CollisionHeader typedef
// conflict between z64bgcheck.h (C typedef) and C++ compilation units.
// ---------------------------------------------------------------------------

static void AppendFromHeader(std::vector<SM64Surface>& out,
                             CollisionHeader* header) {
    if (!header || header->numPolygons == 0 ||
        !header->vtxList || !header->polyList) {
        return;
    }

    out.reserve(out.size() + (size_t)header->numPolygons);

    for (int i = 0; i < header->numPolygons; i++) {
        CollisionPoly* poly = &header->polyList[i];

        // Lower 13 bits of vtxData hold the vertex index
        int vi0 = poly->vtxData[0] & 0x1FFF;
        int vi1 = poly->vtxData[1] & 0x1FFF;
        int vi2 = poly->vtxData[2] & 0x1FFF;

        if (vi0 >= header->numVertices ||
            vi1 >= header->numVertices ||
            vi2 >= header->numVertices) {
            continue;
        }

        Vec3s* v0 = &header->vtxList[vi0];
        Vec3s* v1 = &header->vtxList[vi1];
        Vec3s* v2 = &header->vtxList[vi2];

        uint8_t mat = 0;
        if (header->surfaceTypeList && poly->type < 0x1FF) {
            SurfaceType* st = &header->surfaceTypeList[poly->type & 0xFF];
            mat = (uint8_t)(st->data[0] & 0xFF);
        }

        SM64Surface surf{};
        surf.type    = OotMatToSM64Surface(mat);
        surf.force   = 0;
        surf.terrain = OotMatToSM64Terrain(mat);

        surf.vertices[0][0] = (int32_t)v0->x;
        surf.vertices[0][1] = (int32_t)v0->y;
        surf.vertices[0][2] = (int32_t)v0->z;
        surf.vertices[1][0] = (int32_t)v1->x;
        surf.vertices[1][1] = (int32_t)v1->y;
        surf.vertices[1][2] = (int32_t)v1->z;
        surf.vertices[2][0] = (int32_t)v2->x;
        surf.vertices[2][1] = (int32_t)v2->y;
        surf.vertices[2][2] = (int32_t)v2->z;

        out.push_back(surf);
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::vector<SM64Surface> MarioCollision::BuildSurfacesFromScene(PlayState* play) {
    std::vector<SM64Surface> surfaces;
    surfaces.reserve(8192);

    CollisionHeader* staticHeader = play->colCtx.colHeader;
    if (staticHeader) {
        AppendFromHeader(surfaces, staticHeader);
    }

    printf("[Mario] BuildSurfacesFromScene: %zu surfaces.\n", surfaces.size());
    return surfaces;
}

void MarioCollision::UpdateDynamicObject(uint32_t objId,
                                          float x, float y, float z, float ry) {
    if (objId == UINT32_MAX) return;
    SM64ObjectTransform t{};
    t.position[0] = x;
    t.position[1] = y;
    t.position[2] = z;
    t.eulerRotation[1] = ry;
    sm64_surface_object_move(objId, &t);
}

void MarioCollision::DeleteDynamicObject(uint32_t objId) {
    if (objId != UINT32_MAX) {
        sm64_surface_object_delete(objId);
    }
}

#endif // __EMSCRIPTEN__
