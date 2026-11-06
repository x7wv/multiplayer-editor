// Multiplayer Editor — made by x7wv
// Windows: the socket headers must come before <windows.h> (pulled in by Geode)
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include "Net.hpp"
#include "Certs.hpp"

using namespace geode::prelude;

namespace me {

Net& Net::get() {
    static Net inst;
    return inst;
}

void Net::connect(std::string const& url) {
    static bool netInit = false;
    if (!netInit) {
        ix::initNetSystem();
        netInit = true;
    }
    if (m_ws && m_url == url && m_state != NetState::Offline) return;
    this->disconnect();

    m_url = url;
    m_state = NetState::Connecting;
    int gen = ++m_generation;

    m_ws = std::make_unique<ix::WebSocket>();
    m_ws->setUrl(url);
    if (url.rfind("wss://", 0) == 0) {
        // encrypted connection: verify the server against the built-in root certificates
        ix::SocketTLSOptions tls;
        std::string bundle;
        for (auto cert : kRootCerts) bundle += cert;
        tls.caFile = bundle;
        m_ws->setTLSOptions(tls);
    }
    m_ws->disablePerMessageDeflate();
    m_ws->setPingInterval(20);
    m_ws->enableAutomaticReconnection();
    m_ws->setMinWaitBetweenReconnectionRetries(500);
    m_ws->setMaxWaitBetweenReconnectionRetries(5000);

    m_ws->setOnMessageCallback([this, gen](ix::WebSocketMessagePtr const& msg) {
        // runs on the socket thread -> hop to the main thread
        switch (msg->type) {
            case ix::WebSocketMessageType::Message: {
                std::string data = msg->str;
                geode::queueInMainThread([this, gen, data = std::move(data)] {
                    if (gen != m_generation) return;
                    bytesReceived += data.size();
                    auto parsed = matjson::parse(data);
                    if (!parsed) return;
                    if (onMessage) onMessage(parsed.unwrap());
                });
                break;
            }
            case ix::WebSocketMessageType::Open: {
                geode::queueInMainThread([this, gen] {
                    if (gen != m_generation) return;
                    m_state = NetState::Online;
                    if (onOpen) onOpen();
                });
                break;
            }
            case ix::WebSocketMessageType::Close:
            case ix::WebSocketMessageType::Error: {
                std::string reason = msg->type == ix::WebSocketMessageType::Error
                    ? msg->errorInfo.reason : msg->closeInfo.reason;
                geode::queueInMainThread([this, gen, reason] {
                    if (gen != m_generation) return;
                    bool wasOnline = m_state == NetState::Online;
                    m_state = NetState::Connecting; // auto-reconnect keeps trying
                    if (wasOnline && onClose) onClose(reason);
                });
                break;
            }
            default: break;
        }
    });
    m_ws->start();
}

void Net::disconnect() {
    ++m_generation;
    if (m_ws) {
        m_ws->disableAutomaticReconnection();
        m_ws->stop();
        m_ws.reset();
    }
    m_state = NetState::Offline;
}

void Net::send(matjson::Value const& msg) {
    if (!m_ws || m_state != NetState::Online) return;
    auto data = msg.dump(matjson::NO_INDENTATION);
    bytesSent += data.size();
    m_ws->send(data);
}

}
