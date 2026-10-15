// SPDX-License-Identifier: GPL-3.0-only
#include "system/disksensorprovider.h"

#include <QDir>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QThread>

namespace qsensors {
namespace {
QByteArray readAttribute(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    constexpr qint64 limit = 4096;
    const auto bytes = file.read(limit + 1);
    return file.error() == QFile::NoError && bytes.size() <= limit ? bytes.trimmed() : QByteArray();
}
}

SensorDiscovery DiskSensorProvider::discover()
{
    m_disks.clear();
    m_channels.clear();
    m_clock.start();
    SensorDiscovery result;
    const QDir blocks(QStringLiteral("/sys/block"));
    for (const auto &name : blocks.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (QThread::currentThread()->isInterruptionRequested()) break;
        const QString path = QFileInfo(blocks.filePath(name)).canonicalFilePath();
        // Whole devices backed by a kernel device, not software block layers.
        if (!path.startsWith(QStringLiteral("/sys/devices/"))
            || path.startsWith(QStringLiteral("/sys/devices/virtual/"))
            || QFile::exists(path + QStringLiteral("/partition"))
            || !QFileInfo::exists(path + QStringLiteral("/device"))
            || !QFile::exists(path + QStringLiteral("/stat"))) continue;
        // A bounded, percent-free ID also round-trips through the settings ID
        // serializer, which decodes percent escapes in human-readable IDs.
        const QString diskId = QStringLiteral("system/disk/%1").arg(QString::fromLatin1(
            QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256).toHex()));
        Disk disk;
        disk.path = path;
        disk.sequence = readAttribute(path + QStringLiteral("/diskseq"));
        m_disks.insert(diskId, disk);
        const QString model = QString::fromUtf8(readAttribute(path + QStringLiteral("/device/model"))).simplified();
        const QString source = model.isEmpty() ? QStringLiteral("Disk — %1").arg(name)
            : QStringLiteral("Disk — %1 (%2)").arg(name, model);
        const QStringList suffixes = {QStringLiteral("read"), QStringLiteral("write"), QStringLiteral("busy")};
        const QStringList labels = {QStringLiteral("Read per update"), QStringLiteral("Written per update"), QStringLiteral("Busy")};
        for (int metric = 0; metric < 3; ++metric) {
            const QString id = diskId + QLatin1Char('/') + suffixes[metric];
            m_channels.insert(id, {diskId, metric});
            result.descriptors.append({id, labels[metric], metric == 2 ? SensorKind::Percentage : SensorKind::Bytes,
                                       metric == 2 ? QStringLiteral("%") : QStringLiteral("B"), source});
        }
    }
    return result;
}

QList<SensorSample> DiskSensorProvider::readBatch(const QStringList &ids)
{
    struct Reading { std::optional<systemmetrics::DiskCounters> counters; qint64 nanoseconds; };
    QHash<QString, Reading> readings;
    QList<SensorSample> result;
    for (const auto &id : ids) {
        if (QThread::currentThread()->isInterruptionRequested()) break;
        SensorSample sample;
        sample.id = id;
        sample.timestamp = QDateTime::currentDateTimeUtc();
        const auto channel = m_channels.constFind(id);
        if (channel == m_channels.cend()) {
            sample.error = QStringLiteral("Disk sensor is no longer present; use Rescan");
            result.append(sample);
            continue;
        }
        auto &disk = m_disks[channel->diskId];
        if (!readings.contains(channel->diskId)) {
            const QString sequencePath = disk.path + QStringLiteral("/diskseq");
            Reading reading{{}, m_clock.nsecsElapsed()};
            // Reuse of a kernel device name must not turn replacement counters
            // into a transfer spike. Older kernels may omit diskseq entirely.
            if (disk.sequence.isEmpty() || readAttribute(sequencePath) == disk.sequence) {
                reading.counters = systemmetrics::diskCounters(readAttribute(disk.path + QStringLiteral("/stat")));
                reading.nanoseconds = m_clock.nsecsElapsed();
                if (!disk.sequence.isEmpty() && readAttribute(sequencePath) != disk.sequence)
                    reading.counters.reset();
            }
            readings.insert(channel->diskId, reading);
        }
        const auto &reading = readings[channel->diskId];
        auto &previous = disk.previous[channel->metric];
        if (!reading.counters) {
            previous.reset();
            sample.error = QStringLiteral("Disk statistics unavailable, malformed or device replaced; use Rescan");
        } else {
            if (previous) {
                const auto delta = systemmetrics::diskInterval(previous->counters, *reading.counters,
                    static_cast<double>(reading.nanoseconds - previous->nanoseconds) / 1e6);
                if (delta) {
                    sample.value = channel->metric == 0 ? delta->readBytes
                        : channel->metric == 1 ? delta->writtenBytes : delta->busyPercent;
                    sample.available = true;
                }
            }
            if (!sample.available) sample.error = QStringLiteral("Waiting for a valid disk counter interval");
            previous = Baseline{*reading.counters, reading.nanoseconds};
        }
        sample.detail = channel->metric == 2
            ? QStringLiteral("Kernel I/O busy time as a percentage of elapsed time; not performance saturation")
            : QStringLiteral("Bytes transferred since this channel's previous acquisition; not bytes per second");
        result.append(sample);
    }
    return result;
}

} // namespace qsensors
