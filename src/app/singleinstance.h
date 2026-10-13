// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QLocalServer>
#include <QObject>
#include <QString>

#include <memory>

class QLockFile;

namespace qsensors {

class SingleInstance final : public QObject {
    Q_OBJECT
public:
    enum class Result { Primary, ActivatedExisting, Failed };
    explicit SingleInstance(QObject *parent = nullptr);
    ~SingleInstance() override;
    Result start();
    QString errorString() const { return m_error; }

signals:
    void showRequested();

private:
    void acceptConnections();
    std::unique_ptr<QLockFile> m_lock;
    QLocalServer m_server;
    QString m_endpoint;
    QString m_error;
    int m_connections = 0;
    bool m_primary = false;
};

} // namespace qsensors
