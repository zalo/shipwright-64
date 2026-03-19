#include "Network.h"
#include <spdlog/spdlog.h>
#include <libultraship/libultraship.h>

// MARK: - Public

void Network::Enable(const char* host, uint16_t port) {
#ifdef __EMSCRIPTEN__
    if (isEnabled) {
        return;
    }

    // Build WebSocket URL from host:port.
    // If host already contains a scheme (ws:// or wss://), use as-is with port.
    // Otherwise, default to wss:// for security (browsers block mixed content).
    std::string hostStr(host);
    std::string url;
    if (hostStr.find("ws://") == 0 || hostStr.find("wss://") == 0) {
        url = hostStr + ":" + std::to_string(port);
    } else {
        url = std::string("wss://") + host + ":" + std::to_string(port);
    }
    EnableWebSocket(url);
#elif defined(ENABLE_REMOTE_CONTROL)
    if (isEnabled) {
        return;
    }

    if (SDLNet_ResolveHost(&networkAddress, host, port) == -1) {
        SPDLOG_ERROR("[Network] SDLNet_ResolveHost: {}", SDLNet_GetError());
    }

    isEnabled = true;

    // First check if there is a thread running, if so, join it
    if (receiveThread.joinable()) {
        receiveThread.join();
    }

    receiveThread = std::thread(&Network::ReceiveFromServer, this);
#endif
}

void Network::Disable() {
    if (!isEnabled) {
        return;
    }

    isEnabled = false;

#ifdef __EMSCRIPTEN__
    wsClient.Disconnect();
    isConnected = false;
    OnDisconnected();
#else
    receiveThread.join();
#endif
}

void Network::OnIncomingData(char payload[512]) {
}

void Network::OnIncomingJson(nlohmann::json payload) {
}

void Network::OnConnected() {
}

void Network::OnDisconnected() {
}

void Network::ProcessOutgoingPackets() {
}

void Network::SendDataToRemote(const char* payload) {
#ifdef __EMSCRIPTEN__
    if (isConnected) {
        wsClient.Send(std::string(payload));
    }
#elif defined(ENABLE_REMOTE_CONTROL)
    SPDLOG_DEBUG("[Network] Sending data: {}", payload);
    SDLNet_TCP_Send(networkSocket, payload, strlen(payload) + 1);
#endif
}

void Network::SendJsonToRemote(nlohmann::json payload) {
#ifdef __EMSCRIPTEN__
    if (isConnected) {
        wsClient.Send(payload.dump());
    }
#else
    SendDataToRemote(payload.dump().c_str());
#endif
}

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

void Network::EnableWebSocket(const std::string& url) {
    isEnabled = true;
    wsUrl = url;
    reconnectAttempts = 0;

    wsClient.SetOnConnect([this]() {
        isConnected = true;
        reconnectAttempts = 0;
        reconnectScheduled = false;
        OnConnected();
    });

    wsClient.SetOnDisconnect([this]() {
        isConnected = false;
        if (isEnabled) {
            OnDisconnected();
            ScheduleReconnect();
        }
    });

    wsClient.Connect(url);
}

void Network::ScheduleReconnect() {
    if (reconnectScheduled || !isEnabled || wsUrl.empty()) {
        return;
    }

    reconnectScheduled = true;
    reconnectAttempts++;

    // Exponential backoff: 1s, 2s, 4s, 8s, capped at 15s
    int delayMs = std::min(1000 * (1 << std::min(reconnectAttempts - 1, 3)), 15000);
    SPDLOG_INFO("[Network] WebSocket disconnected. Reconnecting in {}ms (attempt {})", delayMs, reconnectAttempts);

    emscripten_async_call([](void* userData) {
        Network* self = static_cast<Network*>(userData);
        self->reconnectScheduled = false;
        if (self->isEnabled && !self->isConnected) {
            SPDLOG_INFO("[Network] Attempting WebSocket reconnect to {}", self->wsUrl);
            self->wsClient.Connect(self->wsUrl);
        }
    }, this, delayMs);
}

void Network::PollIncoming() {
    if (!isEnabled) {
        return;
    }

    // Process outgoing packets
    ProcessOutgoingPackets();

    // Poll for incoming WebSocket messages
    std::string message;
    while (wsClient.Poll(message)) {
        HandleRemoteJson(message);
    }
}
#endif

// MARK: - Private

void Network::ReceiveFromServer() {
#if !defined(__EMSCRIPTEN__) && defined(ENABLE_REMOTE_CONTROL)
    while (isEnabled) {
        while (!isConnected && isEnabled) {
            SPDLOG_TRACE("[Network] Attempting to make connection to server...");
            networkSocket = SDLNet_TCP_Open(&networkAddress);

            if (networkSocket) {
                isConnected = true;
                receivedData.clear();
                SPDLOG_INFO("[Network] Connection to server established!");

                OnConnected();
                break;
            }
        }

        SDLNet_SocketSet socketSet = SDLNet_AllocSocketSet(1);
        if (networkSocket) {
            SDLNet_TCP_AddSocket(socketSet, networkSocket);
        }

        // Listen to socket messages
        while (isConnected && networkSocket && isEnabled) {
            // we check first if socket has data, to not block in the TCP_Recv
            int socketsReady = SDLNet_CheckSockets(socketSet, 0);

            if (socketsReady == -1) {
                SPDLOG_ERROR("[Network] SDLNet_CheckSockets: {}", SDLNet_GetError());
                break;
            }

            // Always process outgoing packets
            ProcessOutgoingPackets();

            if (socketsReady == 0) {
                // No incoming data
                continue;
            }

            char remoteDataReceived[512];
            memset(remoteDataReceived, 0, sizeof(remoteDataReceived));
            int len = SDLNet_TCP_Recv(networkSocket, &remoteDataReceived, sizeof(remoteDataReceived));
            if (!len || !networkSocket || len == -1) {
                SPDLOG_ERROR("[Network] SDLNet_TCP_Recv: {}", SDLNet_GetError());
                break;
            }

            HandleRemoteData(remoteDataReceived);

            receivedData.append(remoteDataReceived, len);

            // Process all complete packets
            size_t delimiterPos = receivedData.find('\0');
            while (delimiterPos != std::string::npos) {
                std::string packet = receivedData.substr(0, delimiterPos);
                receivedData.erase(0, delimiterPos + 1);
                HandleRemoteJson(packet);
                delimiterPos = receivedData.find('\0');
            }
        }

        if (socketSet) {
            SDLNet_FreeSocketSet(socketSet);
        }

        if (isConnected) {
            SDLNet_TCP_Close(networkSocket);
            networkSocket = nullptr;
            isConnected = false;
            receivedData.clear();
            OnDisconnected();
            SPDLOG_INFO("[Network] Ending receiving thread...");
        }
    }
#endif
}

void Network::HandleRemoteData(char payload[512]) {
    OnIncomingData(payload);
}

void Network::HandleRemoteJson(std::string payload) {
    SPDLOG_DEBUG("[Network] Received json: {}", payload);
    nlohmann::json jsonPayload;
    try {
        jsonPayload = nlohmann::json::parse(payload);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[Network] Failed to parse json: \n{}\n{}\n", payload, e.what());
        return;
    }

    OnIncomingJson(jsonPayload);
}
