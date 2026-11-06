// Multiplayer Editor — made by x7wv
#pragma once
#include <Geode/Geode.hpp>
#include <matjson.hpp>
#include <functional>
#include <memory>
#include <string>

namespace ix { class WebSocket; }

namespace me {

enum class NetState { Offline, Connecting, Online };

// Thin wrapper around a WebSocket. All callbacks run on the GD main thread.
class Net {
public:
    static Net& get();

    void connect(std::string const& url);
    void disconnect();
    void send(matjson::Value const& msg);
    NetState state() const { return m_state; }
    std::string const& url() const { return m_url; }

    std::function<void(matjson::Value const&)> onMessage;
    std::function<void()> onOpen;
    std::function<void(std::string const&)> onClose;

    size_t bytesSent = 0;
    size_t bytesReceived = 0;

private:
    std::unique_ptr<ix::WebSocket> m_ws;
    NetState m_state = NetState::Offline;
    std::string m_url;
    int m_generation = 0;
};

}
