/**
 * Ship of Harkinian - PartyKit Anchor Server
 *
 * A comprehensive multiplayer relay server that replicates the functionality of
 * the native Anchor TCP server (anchor.hm64.org) for web-based play.
 *
 * Key responsibilities:
 * - Track client state (name, color, team, scene, etc.)
 * - Manage room state (pvp, teleport, sync settings)
 * - Route packets: targeted (to specific client), team-based, or broadcast
 * - Maintain a team queue for item/flag sync so offline teammates catch up
 * - Generate ALL_CLIENT_STATE on connect/disconnect
 *
 * Deploy with: npx partykit deploy
 * Or run locally: npx partykit dev
 */

import type { Party, PartyKitServer, Connection } from "partykit/server";

// ---- Types ----

interface ClientState {
  clientId: number;
  name: string;
  color: { r: number; g: number; b: number };
  clientVersion: string;
  teamId: string;
  online: boolean;
  self: boolean;
  seed: number;
  isSaveLoaded: boolean;
  isGameComplete: boolean;
  sceneNum: number;
  entranceIndex: number;
}

interface RoomState {
  ownerClientId: number;
  pvpMode: number;       // 0=off, 1=on, 2=friendly fire
  showLocationsMode: number; // 0=none, 1=team, 2=all
  teleportMode: number;  // 0=off, 1=team, 2=all
  syncItemsAndFlags: number; // 0=off, 1=on
}

// Map connection ID -> clientId for fast lookup
type ConnectionMap = Map<string, number>;
// Map clientId -> connection ID for reverse lookup
type ClientConnectionMap = Map<number, string>;

// ---- Packet Types ----

const PACKET = {
  // Client -> Server
  HANDSHAKE: "HANDSHAKE",
  UPDATE_CLIENT_STATE: "UPDATE_CLIENT_STATE",
  UPDATE_ROOM_STATE: "UPDATE_ROOM_STATE",
  PLAYER_UPDATE: "PLAYER_UPDATE",
  REQUEST_TEAM_STATE: "REQUEST_TEAM_STATE",
  UPDATE_TEAM_STATE: "UPDATE_TEAM_STATE",
  GIVE_ITEM: "GIVE_ITEM",
  SET_FLAG: "SET_FLAG",
  UNSET_FLAG: "UNSET_FLAG",
  SET_CHECK_STATUS: "SET_CHECK_STATUS",
  UPDATE_BEANS_COUNT: "UPDATE_BEANS_COUNT",
  UPDATE_DUNGEON_ITEMS: "UPDATE_DUNGEON_ITEMS",
  ENTRANCE_DISCOVERED: "ENTRANCE_DISCOVERED",
  REQUEST_TELEPORT: "REQUEST_TELEPORT",
  TELEPORT_TO: "TELEPORT_TO",
  DAMAGE_PLAYER: "DAMAGE_PLAYER",
  PLAYER_SFX: "PLAYER_SFX",
  OCARINA_SFX: "OCARINA_SFX",
  GAME_COMPLETE: "GAME_COMPLETE",

  // Server -> Client
  ALL_CLIENT_STATE: "ALL_CLIENT_STATE",
  SERVER_MESSAGE: "SERVER_MESSAGE",
  DISABLE_ANCHOR: "DISABLE_ANCHOR",
} as const;

// Packets that route to a specific targetClientId
const TARGETED_PACKETS = new Set([
  PACKET.PLAYER_UPDATE,
  PACKET.REQUEST_TELEPORT,
  PACKET.TELEPORT_TO,
  PACKET.DAMAGE_PLAYER,
  PACKET.PLAYER_SFX,
  PACKET.OCARINA_SFX,
]);

// Packets that route to team members via targetTeamId
// These also get queued for offline teammates when addToQueue is true
const TEAM_PACKETS = new Set([
  PACKET.GIVE_ITEM,
  PACKET.SET_FLAG,
  PACKET.UNSET_FLAG,
  PACKET.SET_CHECK_STATUS,
  PACKET.UPDATE_BEANS_COUNT,
  PACKET.UPDATE_DUNGEON_ITEMS,
  PACKET.ENTRANCE_DISCOVERED,
]);

// ---- Server ----

export default {
  async onConnect(connection: Connection, room: Party) {
    const clientId = hashConnectionId(connection.id);
    console.log(`[Anchor] Client connected: ${connection.id} (clientId: ${clientId})`);

    // Store clientId on the connection for easy access
    connection.setState({ clientId });

    // Load server state from room storage
    const clients = await getClients(room);
    const roomState = await getRoomState(room);
    const connectionMap = await getConnectionMap(room);
    const clientConnectionMap = await getClientConnectionMap(room);

    // Register connection mapping
    connectionMap.set(connection.id, clientId);
    clientConnectionMap.set(clientId, connection.id);
    await room.storage.put("connectionMap", Object.fromEntries(connectionMap));
    await room.storage.put("clientConnectionMap", Object.fromEntries(clientConnectionMap));

    // Create or update client entry (mark online)
    if (!clients[clientId]) {
      clients[clientId] = {
        clientId,
        name: "",
        color: { r: 100, g: 255, b: 100 },
        clientVersion: "",
        teamId: "default",
        online: true,
        self: false,
        seed: 0,
        isSaveLoaded: false,
        isGameComplete: false,
        sceneNum: -1,
        entranceIndex: 0,
      };
    } else {
      clients[clientId].online = true;
    }
    await room.storage.put("clients", clients);

    // Send WELCOME with clientId so the client knows its identity
    connection.send(JSON.stringify({
      type: "WELCOME",
      clientId,
      roomId: room.id,
    }));

    // Broadcast ALL_CLIENT_STATE to everyone
    broadcastAllClientState(room, clients);
  },

  async onMessage(message: string | ArrayBuffer, sender: Connection, room: Party) {
    if (typeof message !== "string") return;

    let packet: any;
    try {
      packet = JSON.parse(message);
    } catch {
      console.error(`[Anchor] Failed to parse message from ${sender.id}`);
      return;
    }

    if (!packet.type) return;

    const senderClientId = hashConnectionId(sender.id);

    // Inject sender's clientId
    if (packet.clientId == null) {
      packet.clientId = senderClientId;
    }

    const type: string = packet.type;

    // ---- HANDSHAKE ----
    if (type === PACKET.HANDSHAKE) {
      await handleHandshake(packet, senderClientId, room);
      return;
    }

    // ---- UPDATE_CLIENT_STATE ----
    if (type === PACKET.UPDATE_CLIENT_STATE) {
      await handleUpdateClientState(packet, senderClientId, room);
      return;
    }

    // ---- UPDATE_ROOM_STATE ----
    if (type === PACKET.UPDATE_ROOM_STATE) {
      await handleUpdateRoomState(packet, room);
      return;
    }

    // ---- UPDATE_TEAM_STATE ----
    // When a client sends its save state, store it and route to team
    if (type === PACKET.UPDATE_TEAM_STATE) {
      await handleUpdateTeamState(packet, senderClientId, room);
      return;
    }

    // ---- REQUEST_TEAM_STATE ----
    // Route to online teammates so one of them responds with UPDATE_TEAM_STATE
    if (type === PACKET.REQUEST_TEAM_STATE) {
      await handleRequestTeamState(packet, senderClientId, room);
      return;
    }

    // ---- GAME_COMPLETE ----
    if (type === PACKET.GAME_COMPLETE) {
      // Broadcast to all
      broadcastExcept(room, sender.id, packet);
      return;
    }

    // ---- Targeted packets (to specific clientId) ----
    if (TARGETED_PACKETS.has(type) && packet.targetClientId != null) {
      await sendToClient(room, packet.targetClientId, packet);
      return;
    }

    // ---- Team-routed packets ----
    if (TEAM_PACKETS.has(type) && packet.targetTeamId != null) {
      await handleTeamPacket(packet, senderClientId, room);
      return;
    }

    // ---- Default: broadcast to all except sender ----
    broadcastExcept(room, sender.id, packet);
  },

  async onClose(connection: Connection, room: Party) {
    const clientId = hashConnectionId(connection.id);
    console.log(`[Anchor] Client disconnected: ${connection.id} (clientId: ${clientId})`);

    const clients = await getClients(room);
    const connectionMap = await getConnectionMap(room);
    const clientConnectionMap = await getClientConnectionMap(room);

    // Mark client offline (don't remove - preserves state for reconnects)
    if (clients[clientId]) {
      clients[clientId].online = false;
      await room.storage.put("clients", clients);
    }

    // Clean up connection mappings
    connectionMap.delete(connection.id);
    clientConnectionMap.delete(clientId);
    await room.storage.put("connectionMap", Object.fromEntries(connectionMap));
    await room.storage.put("clientConnectionMap", Object.fromEntries(clientConnectionMap));

    // Broadcast updated client list
    broadcastAllClientState(room, clients);
  },

  async onError(connection: Connection, error: Error) {
    console.error(`[Anchor] Error for ${connection.id}:`, error.message);
  },
} satisfies PartyKitServer;

// ---- Packet Handlers ----

async function handleHandshake(
  packet: any,
  senderClientId: number,
  room: Party
) {
  const clients = await getClients(room);
  let roomState = await getRoomState(room);

  // If room has no owner yet, this client owns it and sets the room state
  if (roomState.ownerClientId === 0 || !isClientOnline(clients, roomState.ownerClientId)) {
    if (packet.roomState) {
      roomState = {
        ownerClientId: senderClientId,
        pvpMode: packet.roomState.pvpMode ?? 0,
        showLocationsMode: packet.roomState.showLocationsMode ?? 0,
        teleportMode: packet.roomState.teleportMode ?? 0,
        syncItemsAndFlags: packet.roomState.syncItemsAndFlags ?? 0,
      };
    } else {
      roomState.ownerClientId = senderClientId;
    }
    await room.storage.put("roomState", roomState);
  }

  // Update client state from handshake
  if (packet.clientState) {
    const cs = packet.clientState;
    clients[senderClientId] = {
      ...clients[senderClientId],
      clientId: senderClientId,
      name: cs.name ?? "",
      color: cs.color ?? { r: 100, g: 255, b: 100 },
      clientVersion: cs.clientVersion ?? "",
      teamId: cs.teamId ?? "default",
      online: true,
      self: false,
      seed: cs.seed ?? 0,
      isSaveLoaded: cs.isSaveLoaded ?? false,
      isGameComplete: cs.isGameComplete ?? false,
      sceneNum: cs.sceneNum ?? -1,
      entranceIndex: cs.entranceIndex ?? 0,
    };
    await room.storage.put("clients", clients);
  }

  // Send room state to the connecting client
  const clientConnectionMap = await getClientConnectionMap(room);
  const connId = clientConnectionMap.get(senderClientId);
  if (connId) {
    const conn = getConnectionById(room, connId);
    if (conn) {
      conn.send(JSON.stringify({
        type: PACKET.UPDATE_ROOM_STATE,
        state: roomState,
      }));
    }
  }

  // Broadcast ALL_CLIENT_STATE to everyone
  broadcastAllClientState(room, clients);
}

async function handleUpdateClientState(
  packet: any,
  senderClientId: number,
  room: Party
) {
  const clients = await getClients(room);

  if (clients[senderClientId] && packet.state) {
    const cs = packet.state;
    clients[senderClientId].name = cs.name ?? clients[senderClientId].name;
    clients[senderClientId].color = cs.color ?? clients[senderClientId].color;
    clients[senderClientId].clientVersion = cs.clientVersion ?? clients[senderClientId].clientVersion;
    clients[senderClientId].teamId = cs.teamId ?? clients[senderClientId].teamId;
    clients[senderClientId].online = cs.online ?? true;
    clients[senderClientId].seed = cs.seed ?? 0;
    clients[senderClientId].isSaveLoaded = cs.isSaveLoaded ?? false;
    clients[senderClientId].isGameComplete = cs.isGameComplete ?? false;
    clients[senderClientId].sceneNum = cs.sceneNum ?? -1;
    clients[senderClientId].entranceIndex = cs.entranceIndex ?? 0;
    await room.storage.put("clients", clients);
  }

  // Broadcast to all (the native server does this too)
  const serialized = JSON.stringify(packet);
  for (const conn of room.getConnections()) {
    if (hashConnectionId(conn.id) !== senderClientId) {
      conn.send(serialized);
    }
  }
}

async function handleUpdateRoomState(packet: any, room: Party) {
  if (!packet.state) return;

  const roomState: RoomState = {
    ownerClientId: packet.state.ownerClientId ?? 0,
    pvpMode: packet.state.pvpMode ?? 0,
    showLocationsMode: packet.state.showLocationsMode ?? 0,
    teleportMode: packet.state.teleportMode ?? 0,
    syncItemsAndFlags: packet.state.syncItemsAndFlags ?? 0,
  };
  await room.storage.put("roomState", roomState);

  // Broadcast to all
  const serialized = JSON.stringify({
    type: PACKET.UPDATE_ROOM_STATE,
    clientId: packet.clientId,
    state: roomState,
  });
  for (const conn of room.getConnections()) {
    conn.send(serialized);
  }
}

async function handleUpdateTeamState(
  packet: any,
  senderClientId: number,
  room: Party
) {
  const targetTeamId: string = packet.targetTeamId ?? "default";

  // If the sender is clearing/updating the team queue, process it
  if (packet.queue !== undefined) {
    // Store the team state (save context) for late joiners
    if (packet.state && Object.keys(packet.state).length > 0) {
      await room.storage.put(`teamState:${targetTeamId}`, packet.state);
    }
    // If the queue is empty, the sender has consumed all queued items
    if (Array.isArray(packet.queue) && packet.queue.length === 0) {
      await room.storage.put(`teamQueue:${targetTeamId}`, []);
    }
  }

  // Forward to online team members (except sender)
  const clients = await getClients(room);
  await sendToTeam(room, clients, targetTeamId, senderClientId, packet);
}

async function handleRequestTeamState(
  packet: any,
  senderClientId: number,
  room: Party
) {
  const targetTeamId: string = packet.targetTeamId ?? "default";
  const clients = await getClients(room);

  // Find an online teammate to respond (preferring one with save loaded)
  let responderId: number | null = null;
  for (const [idStr, client] of Object.entries(clients)) {
    const id = Number(idStr);
    if (id !== senderClientId && client.online && client.teamId === targetTeamId && client.isSaveLoaded) {
      responderId = id;
      break;
    }
  }

  if (responderId != null) {
    // Forward the request to the teammate, who will respond with UPDATE_TEAM_STATE
    await sendToClient(room, responderId, packet);
  } else {
    // No online teammates with saves. Send stored team state + queue if available
    const storedState = await room.storage.get(`teamState:${targetTeamId}`) as any;
    const storedQueue = (await room.storage.get(`teamQueue:${targetTeamId}`) as string[]) ?? [];

    if (storedState) {
      await sendToClient(room, senderClientId, {
        type: PACKET.UPDATE_TEAM_STATE,
        clientId: 0, // from server
        targetTeamId,
        state: storedState,
        queue: storedQueue,
      });
    }
  }
}

async function handleTeamPacket(
  packet: any,
  senderClientId: number,
  room: Party
) {
  const targetTeamId: string = packet.targetTeamId;
  const clients = await getClients(room);

  // Add to team queue if requested (for offline teammates to replay later)
  if (packet.addToQueue) {
    const queueKey = `teamQueue:${targetTeamId}`;
    const queue = ((await room.storage.get(queueKey)) as string[]) ?? [];
    queue.push(JSON.stringify(packet));
    await room.storage.put(queueKey, queue);
  }

  // Send to all online team members except sender
  await sendToTeam(room, clients, targetTeamId, senderClientId, packet);
}

// ---- Helpers ----

function hashConnectionId(id: string): number {
  let hash = 0;
  for (let i = 0; i < id.length; i++) {
    const char = id.charCodeAt(i);
    hash = ((hash << 5) - hash) + char;
    hash = hash & 0x7FFFFFFF; // Keep positive 31-bit integer
  }
  return hash;
}

async function getClients(room: Party): Promise<Record<number, ClientState>> {
  return ((await room.storage.get("clients")) as Record<number, ClientState>) ?? {};
}

async function getRoomState(room: Party): Promise<RoomState> {
  return ((await room.storage.get("roomState")) as RoomState) ?? {
    ownerClientId: 0,
    pvpMode: 0,
    showLocationsMode: 0,
    teleportMode: 0,
    syncItemsAndFlags: 0,
  };
}

async function getConnectionMap(room: Party): Promise<ConnectionMap> {
  const raw = (await room.storage.get("connectionMap")) as Record<string, number> | undefined;
  return raw ? new Map(Object.entries(raw).map(([k, v]) => [k, v])) : new Map();
}

async function getClientConnectionMap(room: Party): Promise<ClientConnectionMap> {
  const raw = (await room.storage.get("clientConnectionMap")) as Record<string, string> | undefined;
  return raw ? new Map(Object.entries(raw).map(([k, v]) => [Number(k), v])) : new Map();
}

function getConnectionById(room: Party, connId: string): Connection | null {
  for (const conn of room.getConnections()) {
    if (conn.id === connId) return conn;
  }
  return null;
}

function isClientOnline(clients: Record<number, ClientState>, clientId: number): boolean {
  return clients[clientId]?.online ?? false;
}

function broadcastAllClientState(room: Party, clients: Record<number, ClientState>) {
  // Build per-client payload where each client sees `self: true` for themselves
  for (const conn of room.getConnections()) {
    const viewerClientId = hashConnectionId(conn.id);
    const stateForViewer = Object.values(clients)
      .filter(c => c.online || c.clientId === viewerClientId)
      .map(c => ({
        ...c,
        self: c.clientId === viewerClientId,
      }));

    conn.send(JSON.stringify({
      type: PACKET.ALL_CLIENT_STATE,
      state: stateForViewer,
    }));
  }
}

function broadcastExcept(room: Party, excludeConnId: string, packet: any) {
  const serialized = JSON.stringify(packet);
  for (const conn of room.getConnections()) {
    if (conn.id !== excludeConnId) {
      conn.send(serialized);
    }
  }
}

async function sendToClient(room: Party, targetClientId: number, packet: any) {
  const clientConnectionMap = await getClientConnectionMap(room);
  const connId = clientConnectionMap.get(targetClientId);
  if (!connId) return;

  const conn = getConnectionById(room, connId);
  if (conn) {
    conn.send(JSON.stringify(packet));
  }
}

async function sendToTeam(
  room: Party,
  clients: Record<number, ClientState>,
  teamId: string,
  excludeClientId: number,
  packet: any
) {
  const serialized = JSON.stringify(packet);
  const clientConnectionMap = await getClientConnectionMap(room);

  for (const [idStr, client] of Object.entries(clients)) {
    const id = Number(idStr);
    if (id !== excludeClientId && client.online && client.teamId === teamId) {
      const connId = clientConnectionMap.get(id);
      if (connId) {
        const conn = getConnectionById(room, connId);
        if (conn) {
          conn.send(serialized);
        }
      }
    }
  }
}
