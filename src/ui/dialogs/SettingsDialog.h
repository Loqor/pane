#pragma once

#include <QDialog>

class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;
class QComboBox;
class QStackedWidget;
class QListWidget;

namespace pane {

class MainWindow;

// paint.net-style Settings: a page list on the left, pages on the right.
// Changes apply immediately.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(MainWindow *window);
    // Page names in order (for tests).
    static QStringList pageNames();
    void showPage(int index);

private:
    QWidget *userInterfacePage();
    QWidget *toolsPage();
    QWidget *autosavePage();
    QWidget *keyboardPage();
    QWidget *diagnosticsPage();
    void refreshKeyboard();
    void applyKey(const QKeySequence &seq);
    void fillAccents();

    MainWindow *m_window;
    QStackedWidget *m_stack = nullptr;
    QListWidget *m_pages = nullptr;
    QComboBox *m_theme = nullptr, *m_accent = nullptr;
    QTreeWidget *m_keys = nullptr;
    QKeySequenceEdit *m_keyEdit = nullptr;
    QLabel *m_keyStatus = nullptr;
    QLineEdit *m_keyFilter = nullptr;
};

} // namespace pane
