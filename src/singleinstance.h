#pragma once

#include <QLocalServer>
#include <QObject>

// Keeps Stems to one window. A later `stems` or `stems open FILE` hands FILE
// to the window that's already open, over a socket in the user's runtime
// directory, and exits.
class SingleInstance : public QObject {
    Q_OBJECT

public:
    explicit SingleInstance(const QString &socket = defaultSocket(), QObject *parent = nullptr);

    // $XDG_RUNTIME_DIR/stems.socket, which only this user can reach.
    static QString defaultSocket();

    // Sends a path or link (empty just brings the window forward) to the
    // Stems that's running. False when none is.
    bool forward(const QString &target) const;

    // Starts taking requests from later launches.
    bool listen();

signals:
    void received(const QString &target);

private:
    QString m_socket;
    QLocalServer m_server;
};
