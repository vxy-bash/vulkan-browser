#pragma once

#include <QObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QByteArray>
#include <QString>
#include <memory>
#include <functional>

namespace vkb {

// Message types exchanged between the UI process and a render process.
enum class IpcMsgType : uint8_t {
    Navigate       = 0x01,  // UI  → Render: load URL
    PaintReady     = 0x02,  // Render → UI: pixel data ready in shared memory
    TitleChanged   = 0x03,  // Render → UI
    UrlChanged     = 0x04,  // Render → UI
    LoadStarted    = 0x05,  // Render → UI
    LoadFinished   = 0x06,  // Render → UI (1 byte payload: success flag)
    Resize         = 0x07,  // UI  → Render: new width/height (8 bytes: u32 w, u32 h)
    Shutdown       = 0xFF,
};

#pragma pack(push, 1)
struct IpcHeader {
    IpcMsgType type;
    uint32_t   payloadLen;  // bytes following this header
};
#pragma pack(pop)

// IpcChannel: manages a QLocalSocket connection to one render process.
// The render process is launched as a child, and communicates via
// QLocalSocket on a unique socket name.
class IpcChannel final : public QObject {
    Q_OBJECT

public:
    explicit IpcChannel(QObject* parent = nullptr);
    ~IpcChannel() override;

    // Spawn a new render process and establish the socket connection.
    // Returns true on success.
    bool spawnRenderProcess(const QString& executablePath, const QString& socketName);

    void sendNavigate(const QString& url);
    void sendResize(uint32_t width, uint32_t height);
    void sendShutdown();

signals:
    void messageReceived(QByteArray payload);
    void disconnected();

private slots:
    void onDataReady();
    void onSocketDisconnected();

private:
    void send(IpcMsgType type, const QByteArray& payload = {});

    QLocalServer* m_server{nullptr};
    QLocalSocket* m_socket{nullptr};
    QProcess*     m_renderProcess{nullptr};
    QString       m_socketName;
    QByteArray    m_readBuf;  // accumulates partial reads
};

} // namespace vkb
