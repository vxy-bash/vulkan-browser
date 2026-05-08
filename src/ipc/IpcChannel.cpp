#include "ipc/IpcChannel.h"

#include <QDataStream>
#include <QUuid>
#include <stdexcept>
#include <cstring>

namespace vkb {

IpcChannel::IpcChannel(QObject* parent)
    : QObject(parent)
{}

IpcChannel::~IpcChannel()
{
    sendShutdown();
    if (m_socket)       { m_socket->disconnectFromServer(); }
    if (m_renderProcess){ m_renderProcess->waitForFinished(2000); }
}

bool IpcChannel::spawnRenderProcess(const QString& executablePath,
                                     const QString& socketName)
{
    m_socketName = socketName;
    m_server     = new QLocalServer(this);

    if (!m_server->listen(socketName))
        return false;

    m_renderProcess = new QProcess(this);
    m_renderProcess->start(executablePath,
                           {QStringLiteral("--render-process"),
                            QStringLiteral("--ipc-socket=") + socketName});

    if (!m_server->waitForNewConnection(5000))
        return false;

    m_socket = m_server->nextPendingConnection();
    m_socket->setParent(this);

    connect(m_socket, &QLocalSocket::readyRead,
            this,     &IpcChannel::onDataReady);
    connect(m_socket, &QLocalSocket::disconnected,
            this,     &IpcChannel::onSocketDisconnected);
    return true;
}

void IpcChannel::sendNavigate(const QString& url)
{
    QByteArray payload = url.toUtf8();
    send(IpcMsgType::Navigate, payload);
}

void IpcChannel::sendResize(uint32_t width, uint32_t height)
{
    QByteArray payload(8, Qt::Uninitialized);
    std::memcpy(payload.data(),     &width,  4);
    std::memcpy(payload.data() + 4, &height, 4);
    send(IpcMsgType::Resize, payload);
}

void IpcChannel::sendShutdown()
{
    if (m_socket && m_socket->state() == QLocalSocket::ConnectedState)
        send(IpcMsgType::Shutdown);
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------
void IpcChannel::send(IpcMsgType type, const QByteArray& payload)
{
    if (!m_socket || m_socket->state() != QLocalSocket::ConnectedState)
        return;

    IpcHeader hdr{type, static_cast<uint32_t>(payload.size())};
    m_socket->write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    if (!payload.isEmpty())
        m_socket->write(payload);
    m_socket->flush();
}

void IpcChannel::onDataReady()
{
    m_readBuf += m_socket->readAll();

    while (m_readBuf.size() >= static_cast<qsizetype>(sizeof(IpcHeader))) {
        IpcHeader hdr{};
        std::memcpy(&hdr, m_readBuf.constData(), sizeof(hdr));

        qsizetype totalLen = sizeof(IpcHeader) + hdr.payloadLen;
        if (m_readBuf.size() < totalLen) break;

        QByteArray payload = m_readBuf.mid(sizeof(IpcHeader), hdr.payloadLen);
        m_readBuf.remove(0, static_cast<int>(totalLen));

        emit messageReceived(payload);
    }
}

void IpcChannel::onSocketDisconnected()
{
    emit disconnected();
}

} // namespace vkb
