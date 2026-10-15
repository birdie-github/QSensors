// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "settings/sensorpreferences.h"
#include "sensors/sensortypes.h"
#include <QElapsedTimer>
#include <QPointer>
#include <QTimer>
#include <optional>

class QProcess;

namespace qsensors {

// Alarm state consumes fresh canonical samples, independently of presentation cadence.
class AlarmController final : public QObject {
    Q_OBJECT
public:
    explicit AlarmController(SensorPreferencesStore *preferences, QObject *parent = nullptr);
    void applySnapshot(const SensorSnapshot &snapshot);
    void test(const QString &id, const SensorPreferences &draft);
    std::optional<AlarmSettings> visual(const QString &id) const;
    bool trayHidden(const QString &id) const;
    int testRemaining(const QString &id) const;
    QString lastError(const QString &id) const { return m_errors.value(id); }

signals:
    void visualChanged(const QString &id);
    void trayChanged(const QString &id);
    void testProgress(const QString &id, int milliseconds);
    void notificationRequested(const QString &id, const QString &title, const QString &message);
    void actionError(const QString &id, const QString &message);

private:
    struct State {
        bool low = false;
        bool high = false;
        bool available = false;
        SensorPreferences preferences;
    };
    struct Test { AlarmSettings settings; qint64 expires; };
    struct Blink { bool hidden = false; qint64 next; };
    void preferencesChanged();
    void actions(const QString &id, const SensorPreferences &preferences, const SensorSample &sample, bool test);
    void changed(const QString &id);
    void animate();
    void scheduleAnimation();
    SensorPreferencesStore *m_preferences;
    QHash<QString, State> m_states;
    QHash<QString, QString> m_errors;
    QHash<QString, Test> m_tests;
    QHash<QString, Blink> m_blinks;
    QHash<QString, SensorDescriptor> m_catalog;
    QHash<QString, SensorSample> m_samples;
    QHash<QString, QPointer<QProcess>> m_processes;
    QElapsedTimer m_clock;
    QTimer m_animation;
};

} // namespace qsensors
