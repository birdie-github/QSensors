// SPDX-License-Identifier: GPL-3.0-only
#include "alarms/alarmcontroller.h"
#include "settings/numberformat.h"
#include <QProcess>
#include <QProcessEnvironment>
#include <algorithm>
#include <cmath>
#include <limits>

namespace qsensors {

AlarmController::AlarmController(SensorPreferencesStore *preferences, QObject *parent)
    : QObject(parent), m_preferences(preferences)
{
    m_clock.start();
    connect(this, &AlarmController::actionError, this, [this](const QString &id, const QString &message) {
        m_errors.insert(id, message);
    });
    m_animation.setSingleShot(true);
    m_animation.setTimerType(Qt::PreciseTimer);
    connect(&m_animation, &QTimer::timeout, this, &AlarmController::animate);
    connect(preferences, &SensorPreferencesStore::changed, this, &AlarmController::preferencesChanged);
}

std::optional<AlarmSettings> AlarmController::visual(const QString &id) const
{
    const auto test = m_tests.constFind(id);
    if (test != m_tests.cend()) return test->settings;
    const auto state = m_states.constFind(id);
    if (state != m_states.cend() && state->available && (state->low || state->high))
        return state->preferences.alarm;
    return std::nullopt;
}

bool AlarmController::trayHidden(const QString &id) const
{
    const auto it = m_blinks.constFind(id);
    return it != m_blinks.cend() && it->hidden;
}

int AlarmController::testRemaining(const QString &id) const
{
    const auto it = m_tests.constFind(id);
    return it == m_tests.cend() ? 0 : int(std::max<qint64>(0, it->expires - m_clock.elapsed()));
}

void AlarmController::preferencesChanged()
{
    // Only rearm changed thresholds/calibration. Font, layout, units, and other
    // presentation changes must not produce a second notification/command.
    for (auto it = m_states.begin(); it != m_states.end();) {
        const auto p = m_preferences->value(it.key());
        const auto &old = it->preferences;
        if (!(p.alarm == old.alarm) || p.multiplier != old.multiplier || p.offset != old.offset) {
            const QString id = it.key();
            it = m_states.erase(it);
            m_errors.remove(id);
            changed(id);
        } else {
            const bool trayChanged = old.tray != p.tray;
            it->preferences = p;
            if (trayChanged) changed(it.key());
            ++it;
        }
    }
    for (const auto &id : m_tests.keys()) changed(id);
    updateActiveCount();
}

void AlarmController::applySnapshot(const SensorSnapshot &snapshot)
{
    if (snapshot.rescanned) {
        m_catalog.clear();
        for (const auto &d : snapshot.descriptors) m_catalog.insert(d.id, d);
        for (auto it = m_samples.begin(); it != m_samples.end();) {
            if (!m_catalog.contains(it.key())) it = m_samples.erase(it);
            else ++it;
        }
        for (auto it = m_states.begin(); it != m_states.end(); ++it) {
            if (!m_catalog.contains(it.key()) && it->available) {
                it->available = false;
                changed(it.key());
            }
        }
    }
    for (const auto &sample : snapshot.samples) {
        const auto p = m_preferences->value(sample.id);
        // Retain the latest acquired canonical reading for draft tests, including
        // sensors whose alarm has not yet been saved. Pruned on catalog changes.
        if (m_catalog.contains(sample.id)) m_samples.insert(sample.id, sample);
        if (!p.alarm.enabled()) continue;
        auto &state = m_states[sample.id];
        const bool wasActive = state.low || state.high;
        const bool wasVisible = state.available && wasActive;
        const double value = sample.value * p.multiplier + p.offset;
        state.preferences = p;
        state.available = m_catalog.contains(sample.id) && sample.available
            && sample.timestamp.isValid() && std::isfinite(value);
        if (state.available) {
            const auto &a = p.alarm;
            state.low = a.lowerEnabled && (state.low ? value < a.lower + a.hysteresis : value < a.lower);
            state.high = a.upperEnabled && (state.high ? value > a.upper - a.hysteresis : value > a.upper);
        }
        // Missing readings suppress colors but retain the episode latch. A
        // recovered, still-out-of-range reading does not repeat side effects.
        const bool active = state.low || state.high;
        if (state.available && active && !wasActive) actions(sample.id, p, sample, false);
        if (wasVisible != (state.available && active)) changed(sample.id);
    }
    updateActiveCount();
}

void AlarmController::updateActiveCount()
{
    // Count real, available sensor alarms once per batch, excluding draft tests.
    int count = 0;
    for (const auto &state : m_states)
        if (state.available && (state.low || state.high)) ++count;
    if (count == m_activeCount) return;
    m_activeCount = count;
    emit activeCountChanged(count);
}

void AlarmController::test(const QString &id, const SensorPreferences &draft)
{
    if (id.isEmpty() || !draft.alarm.hasActions()) return;
    if (!draft.alarm.valid()) {
        emit actionError(id, tr("Check the alarm limits, hysteresis, and command before testing."));
        return;
    }
    // A repeated click restarts one bounded overlay; it does not stack timers.
    m_tests.insert(id, {draft.alarm, m_clock.elapsed() + 3000});
    m_blinks.remove(id);
    changed(id);
    emit testProgress(id, 3000);
    actions(id, draft, m_samples.value(id), true);
}

void AlarmController::actions(const QString &id, const SensorPreferences &p,
                              const SensorSample &sample, bool test)
{
    m_errors.remove(id);
    const auto descriptor = m_catalog.value(id);
    const QString name = p.label.isEmpty() ? (descriptor.label.isEmpty() ? id : descriptor.label) : p.label;
    double value = sample.value * p.multiplier + p.offset;
    QString unit = descriptor.unit;
    if (m_catalog.contains(id) && descriptor.kind == SensorKind::Temperature) {
        value = temperatureToDisplay(value, m_preferences->temperatureUnit());
        unit = temperatureUnitText(m_preferences->temperatureUnit());
    }
    const bool valid = sample.available && sample.timestamp.isValid() && std::isfinite(value);
    const QString number = valid ? QString::number(value, 'g', 12) : QString();
    const QString message = valid ? tr("%1: %2 %3").arg(name, numberLocale().toString(value, 'f', standardDecimalPlaces), unit)
                                 : tr("%1: no current reading").arg(name);
    if (p.alarm.notify)
        emit notificationRequested(id, test ? tr("QSensors — Test Alarm") : tr("QSensors — Alarm"), message);
    if (!p.alarm.command) return;
    if (m_processes.value(id)) {
        emit actionError(id, tr("%1: the previous alarm command is still running.").arg(name));
        return;
    }
    auto *process = new QProcess(this);
    m_processes.insert(id, process);
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("SENSOR_ID"), id);
    environment.insert(QStringLiteral("SENSOR_NAME"), name);
    environment.insert(QStringLiteral("SENSOR_VALUE"), number);
    environment.insert(QStringLiteral("SENSOR_UNIT"), unit);
    environment.insert(QStringLiteral("SENSOR_TEST"), test ? QStringLiteral("1") : QStringLiteral("0"));
    process->setProcessEnvironment(environment);
    process->setStandardInputFile(QProcess::nullDevice());
    process->setStandardOutputFile(QProcess::nullDevice());
    process->setStandardErrorFile(QProcess::nullDevice());
    connect(process, &QProcess::errorOccurred, this, [this, id, name, process](QProcess::ProcessError error) {
        emit actionError(id, tr("%1: %2").arg(name, process->errorString()));
        if (error == QProcess::FailedToStart) {
            m_processes.remove(id);
            process->deleteLater();
        }
    });
    connect(process, &QProcess::finished, this, [this, id, name, process](int code, QProcess::ExitStatus status) {
        if (status == QProcess::NormalExit && code != 0)
            emit actionError(id, tr("%1: alarm command exited with code %2.").arg(name).arg(code));
        m_processes.remove(id);
        process->deleteLater();
    });
    process->start(p.alarm.program.trimmed(), QProcess::splitCommand(p.alarm.arguments));
}

void AlarmController::changed(const QString &id)
{
    const auto a = visual(id);
    if (a && a->trayBlink && m_preferences->value(id).tray && sensorSupportsTray(id)) {
        if (!m_blinks.contains(id)) m_blinks.insert(id, {false, m_clock.elapsed() + 1000});
    } else m_blinks.remove(id);
    emit visualChanged(id);
    emit trayChanged(id);
    scheduleAnimation();
}

void AlarmController::animate()
{
    const qint64 now = m_clock.elapsed();
    for (const auto &id : m_tests.keys()) {
        const int remaining = testRemaining(id);
        if (!remaining) {
            m_tests.remove(id);
            changed(id);
        }
        emit testProgress(id, remaining);
    }
    for (auto it = m_blinks.begin(); it != m_blinks.end(); ++it) {
        if (now < it->next) continue;
        it->hidden = !it->hidden;
        it->next = now + (it->hidden ? 750 : 1000);
        emit trayChanged(it.key());
    }
    scheduleAnimation();
}

void AlarmController::scheduleAnimation()
{
    m_animation.stop();
    const qint64 now = m_clock.elapsed();
    qint64 next = std::numeric_limits<qint64>::max();
    for (const auto &blink : m_blinks) next = std::min(next, blink.next);
    for (const auto &test : m_tests) next = std::min(next, std::min(test.expires, now + 100));
    if (next != std::numeric_limits<qint64>::max())
        m_animation.start(int(std::clamp<qint64>(next - now, 1, 1000)));
}

} // namespace qsensors
