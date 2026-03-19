# Enemy AI Sync Feasibility Analysis

## Summary

Synchronizing enemy state across multiplayer clients is **highly feasible** (~2500 LOC, 2-3 weeks). The architecture maps directly onto Anchor's existing DummyPlayer pattern.

## Key Findings

### Why It's Feasible

1. **All enemies use the same targeting pattern**: `GET_PLAYER(play)` + pre-computed `actor->xzDistToPlayer` and `actor->yawTowardsPlayer`. These fields are calculated centrally in `Actor_UpdateAll()` (z_actor.c:2666-2668), so we only need to modify ONE place to make all enemies target the nearest Link.

2. **DummyPlayer already solves the hard problems**: Spawning remote actors, per-frame state sync via JSON packets, collision integration. Enemy sync copies this pattern.

3. **Actor identity is deterministic**: Enemies spawned from room data have the same `(actorId, params, home.pos)` across all clients. This is a reliable matching key.

## Architecture

### Multi-Link Targeting (modify z_actor.c:Actor_UpdateAll)

Currently all enemies compute distance to the single `GET_PLAYER(play)`. Change to iterate all DummyPlayers too and pick the nearest:

```
For each enemy actor:
  minDist = distance(enemy, localPlayer)
  closestLink = localPlayer
  For each DummyPlayer:
    dist = distance(enemy, dummyPlayer)
    if dist < minDist:
      minDist = dist
      closestLink = dummyPlayer
  enemy->xzDistToPlayer = minDist
  enemy->yawTowardsPlayer = yawToward(enemy, closestLink)
```

No enemy AI code changes needed — they already use these pre-computed fields.

### Authority Model

- **Closest Link's client owns the enemy** (sends updates, handles collision)
- **Hysteresis**: require 15-20% distance margin before transferring authority (prevents flapping)
- **Tiebreaker**: lower clientId wins at equal distance
- **Death broadcasts to ALL clients** immediately (not just owner)

### Packet Design

**EnemyUpdate** (sent every frame by owner, per enemy):
| Field | Type | Notes |
|-------|------|-------|
| actorId | s16 | Enemy type (ACTOR_EN_TITE etc) |
| params | s16 | Spawn variant |
| homePos | Vec3s | Initial spawn position (matching key) |
| pos | Vec3f | Current world position |
| rot | Vec3s | Current rotation |
| health | u8 | Current HP |
| isAlive | bool | Dead/alive |
| animIndex | u8 | Current animation |
| animFrame | f32 | Current frame in animation |

**EnemyDeath** (broadcast to all clients):
| Field | Type | Notes |
|-------|------|-------|
| actorId + params + homePos | — | Matching key |
| killerClientId | u32 | Who killed it |

### Non-Owner Client Behavior

- Skip local AI update for synced enemies
- Apply position/rotation/animation from packets
- Skip collision AT/AC checks (owner handles)
- On death packet: immediately kill local enemy actor

## Implementation Phases

| Phase | Description | LOC | Time |
|-------|-------------|-----|------|
| 1 | Multi-Link distance in Actor_UpdateAll | ~50 | 1 day |
| 2 | EnemyUpdate packet (send/receive) | ~200 | 2 days |
| 3 | Authority assignment + hysteresis | ~150 | 2 days |
| 4 | Non-owner state application | ~300 | 2 days |
| 5 | Death sync (broadcast) | ~100 | 1 day |
| 6 | Animation sync | ~200 | 2 days |
| 7 | Collision handling (owner-only) | ~400 | 3 days |
| 8 | Testing + edge cases | ~800 | 3-5 days |

**Recommended start**: Phase 1-3 with a single enemy type (Tektite) as proof of concept.

## Challenges

| Challenge | Risk | Mitigation |
|-----------|------|------------|
| Variable enemy spawns (spawners) | Medium | Sync spawn flags globally first |
| Authority flapping | Low | Distance hysteresis + clientId tiebreak |
| Animation drift | Low | Sync anim state every frame |
| Collision race conditions | Medium | Owner-only collision, broadcast results |
| Network latency (>200ms) | Medium | Position interpolation on non-owner |

## Key Files

| File | Purpose |
|------|---------|
| `soh/src/code/z_actor.c:2573-2725` | Actor_UpdateAll — distance calc to modify |
| `soh/include/z64actor.h:212-269` | Actor struct — fields to sync |
| `soh/soh/Network/Anchor/Packets/PlayerUpdate.cpp` | Template for EnemyUpdate packet |
| `soh/soh/Network/Anchor/DummyPlayer.cpp` | Pattern for remote actor management |
| `soh/soh/Network/Anchor/HookHandlers.cpp` | Hook registration pattern |
