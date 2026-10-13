// SPDX-License-Identifier: GPL-3.0-only
#include "app/singleinstance.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLocalSocket>
#include <QLockFile>
#include <QStandardPaths>
#include <QTimer>
#include <QThread>
#include <QVariant>
#include <algorithm>
#include <memory>

namespace qsensors {

SingleInstance::SingleInstance(QObject *parent) : QObject(parent)
{
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    m_server.setMaxPendingConnections(8);
    connect(&m_server, &QLocalServer::newConnection, this, &SingleInstance::acceptConnections);
}

SingleInstance::~SingleInstance()
{
    // Finish socket destruction while the connection counter/context is alive.
    for (auto *socket : m_server.findChildren<QLocalSocket *>())
        delete socket;
    if (m_primary) {
        m_server.close();
        QLocalServer::removeServer(m_endpoint); // Still protected by our lifetime lock.
    }
}

SingleInstance::Result SingleInstance::start()
{
    QString directory = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    const bool fallback = directory.isEmpty();
    if (fallback)
        directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
            + QStringLiteral("/instance");
    if (!QDir().mkpath(directory)) {
        m_error = tr("Cannot create the single-instance directory.");
        return Result::Failed;
    }
    // RuntimeLocation is user-private; the fallback is explicitly restricted.
    if (fallback && !QFile::setPermissions(directory, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) {
        m_error = tr("Cannot restrict access to the single-instance directory.");
        return Result::Failed;
    }
    const QByteArray session = qgetenv("XDG_SESSION_ID") + '\n' + qgetenv("WAYLAND_DISPLAY")
        + '\n' + qgetenv("DISPLAY");
    const QString key = QString::fromLatin1(QCryptographicHash::hash(session, QCryptographicHash::Sha256).toHex().left(16));
    m_endpoint = directory + QStringLiteral("/qsensors-%1.sock").arg(key);
    m_lock = std::make_unique<QLockFile>(directory + QStringLiteral("/qsensors-%1.lock").arg(key));
    m_lock->setStaleLockTime(0); // Never steal a live instance's lock based on age.
    bool ownsLock = m_lock->tryLock(0);
    if (!ownsLock && m_lock->error() != QLockFile::LockFailedError) {
        m_error = tr("Cannot acquire the single-instance lock.");
        return Result::Failed;
    }
    QElapsedTimer deadline;
    deadline.start();
    while (!ownsLock && deadline.elapsed() < 3000) {
        // Another launch may own the lock before entering its event loop.
        QLocalSocket socket;
        socket.setReadBufferSize(8);
        socket.connectToServer(m_endpoint);
        if (socket.waitForConnected(static_cast<int>(std::max<qint64>(1, std::min<qint64>(200, 3000 - deadline.elapsed()))))) {
            socket.write("show\n");
            socket.flush();
            QByteArray reply;
            QElapsedTimer responseDeadline;
            responseDeadline.start();
            while (responseDeadline.elapsed() < 1000 && deadline.elapsed() < 3000 && reply.size() < 3) {
                if (!socket.bytesAvailable()
                    && !socket.waitForReadyRead(static_cast<int>(std::max<qint64>(1,
                        std::min(1000 - responseDeadline.elapsed(), 3000 - deadline.elapsed())))))
                    break;
                reply += socket.readAll();
            }
            if (reply == "ok\n")
                return Result::ActivatedExisting;
        }
        // The old instance may have exited while we were connecting.
        ownsLock = m_lock->tryLock(0);
        if (!ownsLock)
            QThread::msleep(50); // Startup only, bounded by the three-second deadline.
    }
    if (!ownsLock) {
        m_error = tr("Another QSensors instance holds the lock but did not acknowledge activation.");
        return Result::Failed;
    }
    // Only a lock owner may remove a stale endpoint. Concurrent launches cannot
    // unlink the live primary instance's socket or create duplicate tray items.
    QLocalServer::removeServer(m_endpoint);
    if (!m_server.listen(m_endpoint)) {
        m_error = tr("Cannot create the single-instance socket: %1").arg(m_server.errorString());
        return Result::Failed;
    }
    m_primary = true;
    return Result::Primary;
}

void SingleInstance::acceptConnections()
{
    while (auto *socket = m_server.nextPendingConnection()) {
        if (m_connections >= 8) {
            socket->abort();
            socket->deleteLater();
            continue;
        }
        ++m_connections;
        socket->setReadBufferSize(16);
        connect(socket, &QObject::destroyed, this, [this] { --m_connections; });
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        auto buffer = std::make_shared<QByteArray>();
        auto receive = [this, socket, buffer] {
            if (socket->property("handled").toBool())
                return;
            *buffer += socket->readAll();
            if (buffer->size() > 8) {
                socket->abort();
                socket->deleteLater();
            } else if (buffer->contains('\n')) {
                socket->setProperty("handled", true);
                if (*buffer == "show\n") {
                    emit showRequested();
                    socket->write("ok\n");
                    socket->flush();
                }
                socket->disconnectFromServer();
            }
        };
        connect(socket, &QLocalSocket::readyRead, socket, receive);
        if (socket->bytesAvailable())
            receive();
        QTimer::singleShot(1500, socket, [socket] {
            socket->abort();
            socket->deleteLater();
        });
    }
}

} // namespace qsensors
