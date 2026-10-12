// SPDX-License-Identifier: GPL-3.0-only
#include "sensors/lmsensorsprovider.h"

#include <QByteArray>
#include <QFileInfo>
#include <QRegularExpression>
#include <QThread>
#include <QUrl>
#include <sensors/error.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace qsensors {
namespace {

QString chipName(const sensors_chip_name *chip)
{
    const int length = sensors_snprintf_chip_name(nullptr, 0, chip);
    if (length < 0)
        return {};
    QByteArray name(length + 1, '\0');
    if (sensors_snprintf_chip_name(name.data(), static_cast<size_t>(name.size()), chip) < 0)
        return {};
    return QString::fromUtf8(name.constData());
}

QString deviceIdentity(const sensors_chip_name *chip, const QString &name)
{
    QString path = chip->path ? QFileInfo(QString::fromLocal8Bit(chip->path)).canonicalFilePath() : QString();
    // hwmon indices are assigned at discovery and cannot identify hardware.
    path.remove(QRegularExpression(QStringLiteral("/hwmon/hwmon[0-9]+$")));
    if (path.endsWith(QStringLiteral("/hwmon")))
        path.chop(6);
    if (path.isEmpty() || path == QStringLiteral("/sys/devices/virtual"))
        return name; // Full chip name includes bus number and address.
    return QStringLiteral("%1:%2:%3:%4")
        .arg(QString::fromUtf8(chip->prefix)).arg(chip->bus.type).arg(chip->addr).arg(path);
}

struct InputType {
    sensors_feature_type feature;
    sensors_subfeature_type input;
    SensorKind kind;
    const char *unit;
};

const InputType inputs[] = {
    {SENSORS_FEATURE_TEMP, SENSORS_SUBFEATURE_TEMP_INPUT, SensorKind::Temperature, "°C"},
    {SENSORS_FEATURE_FAN, SENSORS_SUBFEATURE_FAN_INPUT, SensorKind::Fan, "RPM"},
    {SENSORS_FEATURE_IN, SENSORS_SUBFEATURE_IN_INPUT, SensorKind::Voltage, "V"},
    {SENSORS_FEATURE_CURR, SENSORS_SUBFEATURE_CURR_INPUT, SensorKind::Current, "A"},
    {SENSORS_FEATURE_POWER, SENSORS_SUBFEATURE_POWER_INPUT, SensorKind::Power, "W"},
    {SENSORS_FEATURE_POWER, SENSORS_SUBFEATURE_POWER_AVERAGE, SensorKind::Power, "W"},
    {SENSORS_FEATURE_ENERGY, SENSORS_SUBFEATURE_ENERGY_INPUT, SensorKind::Energy, "J"},
    {SENSORS_FEATURE_HUMIDITY, SENSORS_SUBFEATURE_HUMIDITY_INPUT, SensorKind::Humidity, "%"}
};

} // namespace

LmSensorsProvider::~LmSensorsProvider()
{
    cleanup();
}

void LmSensorsProvider::cleanup()
{
    m_channels.clear(); // Discard every library pointer before cleanup.
    if (m_initialized)
        sensors_cleanup();
    m_initialized = false;
}

SensorDiscovery LmSensorsProvider::discover()
{
    cleanup();
    SensorDiscovery result;
    const int error = sensors_init(nullptr); // Default config, labels, ignores and compute rules.
    if (error != 0) {
        result.error = QStringLiteral("libsensors initialization failed: %1")
            .arg(QString::fromLocal8Bit(sensors_strerror(error)));
        // libsensors cleans partial initialization on failure.
        return result;
    }
    m_initialized = true;
    int chipIndex = 0;
    while (const auto *chip = sensors_get_detected_chips(nullptr, &chipIndex)) {
        if (QThread::currentThread()->isInterruptionRequested())
            break;
        const QString name = chipName(chip);
        if (name.isEmpty()) {
            result.error = QStringLiteral("libsensors returned an invalid chip identity");
            break;
        }
        const QString identity = deviceIdentity(chip, name);
        int featureIndex = 0;
        while (const auto *feature = sensors_get_features(chip, &featureIndex)) {
            for (const auto &type : inputs) {
                if (feature->type != type.feature)
                    continue;
                const auto *input = sensors_get_subfeature(chip, feature, type.input);
                if (!input || !(input->flags & SENSORS_MODE_R))
                    continue; // Input-only features require no thresholds.
                const QString id = QStringLiteral("libsensors/%1/%2")
                    .arg(QString::fromLatin1(QUrl::toPercentEncoding(identity)),
                         QString::fromLatin1(QUrl::toPercentEncoding(QString::fromUtf8(input->name))));
                if (m_channels.contains(id)) {
                    result.error = QStringLiteral("Duplicate hardware channel identity: %1").arg(id);
                    break; // Never silently merge two physical channels.
                }
                char *rawLabel = sensors_get_label(chip, feature);
                QString label = rawLabel ? QString::fromUtf8(rawLabel) : QString::fromUtf8(feature->name);
                std::free(rawLabel);
                if (type.input == SENSORS_SUBFEATURE_POWER_AVERAGE)
                    label += QStringLiteral(" (average)");
                result.descriptors.append({id, label, type.kind, QString::fromUtf8(type.unit), name});
                m_channels.insert(id, {chip, input->number});
            }
            if (!result.error.isEmpty())
                break;
        }
        if (!result.error.isEmpty())
            break;
    }
    if (!result.error.isEmpty()) {
        result.descriptors.clear();
        cleanup();
    }
    std::sort(result.descriptors.begin(), result.descriptors.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
    return result;
}

QList<SensorSample> LmSensorsProvider::readBatch(const QStringList &ids)
{
    QList<SensorSample> samples;
    samples.reserve(ids.size());
    for (const auto &id : ids) {
        if (QThread::currentThread()->isInterruptionRequested())
            break;
        SensorSample sample;
        sample.id = id;
        const auto channel = m_channels.constFind(id);
        if (channel == m_channels.cend()) {
            sample.error = QStringLiteral("Sensor is no longer present");
        } else {
            double value = std::numeric_limits<double>::quiet_NaN();
            const int error = sensors_get_value(channel->chip, channel->subfeature, &value);
            if (error != 0)
                sample.error = QString::fromLocal8Bit(sensors_strerror(error));
            else if (!std::isfinite(value))
                sample.error = QStringLiteral("Non-finite hardware reading");
            else {
                sample.value = value;
                sample.available = true;
            }
        }
        sample.timestamp = QDateTime::currentDateTimeUtc();
        samples.append(sample);
    }
    return samples;
}

} // namespace qsensors
