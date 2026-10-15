#pragma once

#include "ui/Workspace.h"

#include <QDateTime>
#include <QObject>
#include <QTimer>
#include <functional>
#include <set>

namespace pane {

// Periodically snapshots changed documents to a recovery folder (as layered
// OpenRaster) and offers them back after a crash.
class AutosaveManager : public QObject {
    Q_OBJECT
public:
    using WorkspaceList = std::function<std::vector<Workspace *>()>;
    AutosaveManager(WorkspaceList workspaces, QObject *parent = nullptr);
    ~AutosaveManager() override;

    static QString recoveryDir();
    void setEnabled(bool on);
    bool isEnabled() const { return m_timer.isActive(); }
    void setIntervalMinutes(int minutes);
    int intervalMinutes() const { return m_timer.interval() / 60000; }

    // Snapshots every changed document now (encoding runs in the background).
    void autosaveNow();
    // Deletes a workspace's recovery files (after save or close).
    void forget(Workspace &ws);
    // Blocks until pending background writes finish (tests, shutdown).
    void waitForPending();

    struct Entry {
        QString id, name, path, fileTypeId;
        QDateTime saved;
        qint64 pid = 0;
    };
    // Recovery entries left behind by Pane processes that are no longer running.
    static std::vector<Entry> orphans();
    static std::unique_ptr<Document> recover(const Entry &e);
    static void discard(const Entry &e);

signals:
    void autosaved(int documents);

private:
    WorkspaceList m_workspaces;
    QTimer m_timer;
    std::set<QString> m_inFlight;
    int m_pending = 0;
};

} // namespace pane
