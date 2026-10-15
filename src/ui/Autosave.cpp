// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#include "ui/Autosave.h"

#include "io/FileType.h"
#include "io/Formats.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QThread>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>

namespace pane {

namespace {

QString oraPath(const QString &id) { return QDir(AutosaveManager::recoveryDir()).filePath(id + QStringLiteral(".ora")); }
QString jsonPath(const QString &id) { return QDir(AutosaveManager::recoveryDir()).filePath(id + QStringLiteral(".json")); }

// True if `pid` is a running Pane process (Linux /proc).
bool paneProcessAlive(qint64 pid)
{
    if (pid <= 0) return false;
    if (pid == QCoreApplication::applicationPid()) return true;
    QFile comm(QStringLiteral("/proc/%1/comm").arg(pid));
    if (!comm.open(QIODevice::ReadOnly)) return false;
    const QByteArray name = comm.readAll().trimmed();
    return name.contains("pane"); // "pane", ".pane-wrapped", test binaries
}

} // namespace

AutosaveManager::AutosaveManager(WorkspaceList workspaces, QObject *parent) : QObject(parent), m_workspaces(std::move(workspaces))
{
    m_timer.setInterval(5 * 60000);
    connect(&m_timer, &QTimer::timeout, this, &AutosaveManager::autosaveNow);
}

AutosaveManager::~AutosaveManager() { waitForPending(); }

QString AutosaveManager::recoveryDir()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/recovery");
    QDir().mkpath(dir);
    return dir;
}

void AutosaveManager::setEnabled(bool on)
{
    if (on) m_timer.start();
    else m_timer.stop();
}

void AutosaveManager::setIntervalMinutes(int minutes)
{
    bool active = m_timer.isActive();
    m_timer.setInterval(std::clamp(minutes, 1, 120) * 60000);
    if (active) m_timer.start();
}

void AutosaveManager::autosaveNow()
{
    int started = 0;
    for (Workspace *ws : m_workspaces()) {
        Document &doc = *ws->doc;
        if (!doc.isDirty()) {
            if (!ws->recoveryId.isEmpty()) forget(*ws); // saved since the last autosave
            continue;
        }
        if (ws->autosavedRevision == doc.revision() && !ws->recoveryId.isEmpty()) continue;
        if (ws->recoveryId.isEmpty()) ws->recoveryId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString id = ws->recoveryId;
        if (m_inFlight.count(id)) continue; // previous write still running; catch it next time
        ws->autosavedRevision = doc.revision();
        std::shared_ptr<Document> snap(doc.snapshot().release(), [](Document *d) {
            // Delete on the thread that owns the QObject.
            QMetaObject::invokeMethod(qApp, [d] { delete d; }, Qt::QueuedConnection);
        });
        QJsonObject meta{{QStringLiteral("pid"), QCoreApplication::applicationPid()},
                         {QStringLiteral("name"), doc.displayName()},
                         {QStringLiteral("path"), doc.filePath()},
                         {QStringLiteral("fileType"), doc.fileTypeId()},
                         {QStringLiteral("saved"), QDateTime::currentDateTime().toString(Qt::ISODate)}};
        m_inFlight.insert(id);
        ++m_pending;
        ++started;
        (void)QtConcurrent::run([this, snap, id, meta] {
            bool ok = true;
            try {
                QByteArray ora = oraFileType()->save(*snap, {}, {});
                filetypes::writeFileAtomically(oraPath(id), ora);
                filetypes::writeFileAtomically(jsonPath(id), QJsonDocument(meta).toJson());
            } catch (const std::exception &e) {
                ok = false;
                qWarning("Autosave failed: %s", e.what());
            }
            QMetaObject::invokeMethod(this, [this, id, ok] {
                m_inFlight.erase(id);
                --m_pending;
                if (ok) emit autosaved(1);
            }, Qt::QueuedConnection);
        });
    }
    (void)started;
}

void AutosaveManager::forget(Workspace &ws)
{
    if (ws.recoveryId.isEmpty()) return;
    const QString id = ws.recoveryId;
    // A write may still be in flight; remove after it lands.
    auto remove = [id] {
        QFile::remove(oraPath(id));
        QFile::remove(jsonPath(id));
    };
    if (m_inFlight.count(id)) {
        waitForPending();
    }
    remove();
    ws.recoveryId.clear();
    ws.autosavedRevision = 0;
}

void AutosaveManager::waitForPending()
{
    while (m_pending > 0) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
}

std::vector<AutosaveManager::Entry> AutosaveManager::orphans()
{
    std::vector<Entry> out;
    QDir dir(recoveryDir());
    for (const QString &f : dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Time)) {
        QFile file(dir.filePath(f));
        if (!file.open(QIODevice::ReadOnly)) continue;
        QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
        Entry e;
        e.id = QFileInfo(f).completeBaseName();
        e.pid = o.value(QStringLiteral("pid")).toInteger();
        e.name = o.value(QStringLiteral("name")).toString();
        e.path = o.value(QStringLiteral("path")).toString();
        e.fileTypeId = o.value(QStringLiteral("fileType")).toString();
        e.saved = QDateTime::fromString(o.value(QStringLiteral("saved")).toString(), Qt::ISODate);
        if (paneProcessAlive(e.pid)) continue; // belongs to a running instance
        if (!QFile::exists(oraPath(e.id))) {
            QFile::remove(dir.filePath(f));
            continue;
        }
        out.push_back(e);
    }
    return out;
}

std::unique_ptr<Document> AutosaveManager::recover(const Entry &e)
{
    QFile f(oraPath(e.id));
    if (!f.open(QIODevice::ReadOnly)) throw FileError("The recovery file could not be read.");
    auto doc = oraFileType()->load(f.readAll());
    if (!e.path.isEmpty()) doc->setFilePath(e.path);
    else doc->setUntitledName(e.name);
    doc->setFileTypeId(e.fileTypeId);
    doc->markDirty();
    return doc;
}

void AutosaveManager::discard(const Entry &e)
{
    QFile::remove(oraPath(e.id));
    QFile::remove(jsonPath(e.id));
}

} // namespace pane
