#include "singleinstance.h"

#include <QLocalSocket>
#include <QStandardPaths>

namespace {
// A path or a link is far shorter; anything longer isn't a request.
constexpr qint64 MaxRequest = 64 * 1024;
}

SingleInstance::SingleInstance(const QString &socket, QObject *parent) : QObject(parent), m_socket(socket) {
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *client = m_server.nextPendingConnection()) {
            connect(client, &QLocalSocket::readyRead, client, [client] {
                if (client->bytesAvailable() > MaxRequest)
                    client->abort();
            });
            connect(client, &QLocalSocket::disconnected, this, [this, client] {
                const QByteArray request = client->readAll();
                client->deleteLater();
                if (request.endsWith('\n'))
                    emit received(QString::fromUtf8(request.chopped(1)));
            });
        }
    });
}

QString SingleInstance::defaultSocket() {
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + "/stems.socket";
}

bool SingleInstance::forward(const QString &target) const {
    QLocalSocket socket;
    socket.connectToServer(m_socket);
    if (!socket.waitForConnected(1000))
        return false;
    socket.write(target.toUtf8() + '\n');
    socket.disconnectFromServer();
    return socket.state() == QLocalSocket::UnconnectedState || socket.waitForDisconnected(1000);
}

bool SingleInstance::listen() {
    if (m_server.listen(m_socket))
        return true;
    // Nothing answered forward(), so the socket is left over from a Stems
    // that crashed. shortcut: two Stems started in the same instant can both
    // get here; the second takes the socket and the first stops getting requests.
    QLocalServer::removeServer(m_socket);
    return m_server.listen(m_socket);
}
