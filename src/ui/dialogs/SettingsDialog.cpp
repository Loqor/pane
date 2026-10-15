#include "ui/dialogs/SettingsDialog.h"

#include "core/Glyphs.h"
#include "tools/Tool.h"
#include "ui/Autosave.h"
#include "ui/FloatingPanel.h"
#include "ui/MainWindow.h"
#include "ui/theme/Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QThread>
#include <QThreadPool>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace pane {

namespace {
constexpr int RoleId = Qt::UserRole, RoleIsTool = Qt::UserRole + 1;
}

QStringList SettingsDialog::pageNames()
{
    return {QStringLiteral("User Interface"), QStringLiteral("Tools & Tablet"), QStringLiteral("Autosave"),
            QStringLiteral("Keyboard"), QStringLiteral("Diagnostics")};
}

SettingsDialog::SettingsDialog(MainWindow *window) : QDialog(window), m_window(window)
{
    setWindowTitle(QStringLiteral("Settings"));
    resize(760, 520);
    auto *h = new QHBoxLayout;
    m_pages = new QListWidget(this);
    m_pages->setFixedWidth(170);
    const QString glyph[] = {glyphs::Palette, glyphs::Brush, glyphs::Save, glyphs::Settings, glyphs::Help};
    for (int i = 0; i < pageNames().size(); ++i) {
        auto *item = new QListWidgetItem(pageNames()[i]);
        item->setIcon(QIcon()); // text-only list; glyphs are shown in page titles
        item->setData(Qt::UserRole, glyph[i]);
        m_pages->addItem(item);
    }
    m_stack = new QStackedWidget(this);
    m_stack->addWidget(userInterfacePage());
    m_stack->addWidget(toolsPage());
    m_stack->addWidget(autosavePage());
    m_stack->addWidget(keyboardPage());
    m_stack->addWidget(diagnosticsPage());
    h->addWidget(m_pages);
    h->addWidget(m_stack, 1);
    auto *v = new QVBoxLayout(this);
    v->addLayout(h, 1);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::accept);
    v->addWidget(bb);
    connect(m_pages, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);
    m_pages->setCurrentRow(0);
}

void SettingsDialog::showPage(int index) { m_pages->setCurrentRow(index); }

static QWidget *titled(QWidget *parent, const QString &title, QLayout *content, bool fill = false)
{
    auto *page = new QWidget(parent);
    auto *v = new QVBoxLayout(page);
    auto *t = new QLabel(QStringLiteral("<h3>%1</h3>").arg(title), page);
    v->addWidget(t);
    v->addLayout(content, fill ? 1 : 0);
    if (!fill) v->addStretch();
    return page;
}

// ------------------------------------------------------------ User interface

QWidget *SettingsDialog::userInterfacePage()
{
    auto *form = new QFormLayout;
    ThemeEngine &te = ThemeEngine::instance();
    m_theme = new QComboBox(this);
    for (const QString &id : te.themeIds()) m_theme->addItem(te.theme(id)->name, id);
    m_theme->setCurrentIndex(m_theme->findData(te.currentId()));
    m_accent = new QComboBox(this);
    fillAccents();
    form->addRow(QStringLiteral("Theme:"), m_theme);
    form->addRow(QStringLiteral("Accent color:"), m_accent);
    connect(m_theme, &QComboBox::currentIndexChanged, this, [this] {
        m_window->applyTheme(m_theme->currentData().toString(), {});
        fillAccents();
    });
    connect(m_accent, &QComboBox::activated, this, [this] {
        m_window->applyTheme(m_theme->currentData().toString(), m_accent->currentData().toString());
    });
    auto *translucent = new QCheckBox(QStringLiteral("Make panels translucent while drawing"), this);
    QAction *ta = m_window->action(QStringLiteral("view.translucent"));
    translucent->setChecked(ta && ta->isChecked());
    connect(translucent, &QCheckBox::toggled, this, [ta](bool on) {
        if (ta) ta->setChecked(on);
        QSettings().setValue(QStringLiteral("Panels/translucent"), on);
    });
    form->addRow(translucent);
    auto *reset = new QPushButton(QStringLiteral("Reset panel positions"), this);
    connect(reset, &QPushButton::clicked, m_window->panelHost(), &FloatingPanelHost::resetPositions);
    form->addRow(reset);
    return titled(this, QStringLiteral("User Interface"), form);
}

void SettingsDialog::fillAccents()
{
    QSignalBlocker b(m_accent);
    m_accent->clear();
    const ThemeEngine &te = ThemeEngine::instance();
    const Theme &cur = te.current();
    if (cur.catppuccin) {
        for (const QString &name : ThemeEngine::catppuccinAccentNames()) {
            QPixmap sw(14, 14);
            sw.fill(cur.accents.at(name));
            QString label = name;
            label[0] = label[0].toUpper();
            m_accent->addItem(QIcon(sw), label, name);
        }
    } else {
        const std::pair<const char *, const char *> presets[] = {{"Default", ""}, {"Teal", "#00897b"}, {"Purple", "#7b5cd6"},
                                                                 {"Pink", "#d6336c"}, {"Orange", "#e8590c"}, {"Green", "#2f9e44"}};
        for (auto &[label, hex] : presets) {
            QPixmap sw(14, 14);
            sw.fill(*hex ? QColor(QString::fromLatin1(hex)) : te.theme(cur.id)->accent);
            m_accent->addItem(QIcon(sw), QString::fromLatin1(label), QString::fromLatin1(hex));
        }
    }
    int idx = m_accent->findData(te.accentName());
    m_accent->setCurrentIndex(std::max(0, idx));
}

// ------------------------------------------------------------ Tools & tablet

QWidget *SettingsDialog::toolsPage()
{
    auto *form = new QFormLayout;
    AppState &app = AppState::instance();
    const ToolSettings &s = app.settings();
    auto commit = [&app] {
        app.notifySettingsChanged();
        app.save();
    };
    auto *size = new QCheckBox(QStringLiteral("Pen pressure changes brush size"), this);
    size->setChecked(s.pressureSize);
    connect(size, &QCheckBox::toggled, this, [&app, commit](bool on) { app.mutableSettings().pressureSize = on; commit(); });
    auto *opacity = new QCheckBox(QStringLiteral("Pen pressure changes opacity"), this);
    opacity->setChecked(s.pressureOpacity);
    connect(opacity, &QCheckBox::toggled, this, [&app, commit](bool on) { app.mutableSettings().pressureOpacity = on; commit(); });
    auto *curve = new QSlider(Qt::Horizontal, this);
    curve->setRange(30, 300);
    curve->setValue(int(std::lround(s.pressureGamma * 100)));
    auto *curveLabel = new QLabel(this);
    auto setCurveLabel = [curveLabel](int v) {
        curveLabel->setText(v < 95 ? QStringLiteral("Soft (%1)").arg(v / 100.0, 0, 'f', 2)
                            : v > 105 ? QStringLiteral("Firm (%1)").arg(v / 100.0, 0, 'f', 2)
                                      : QStringLiteral("Linear"));
    };
    setCurveLabel(curve->value());
    connect(curve, &QSlider::valueChanged, this, [&app, commit, setCurveLabel](int v) {
        app.mutableSettings().pressureGamma = v / 100.0;
        setCurveLabel(v);
        commit();
    });
    auto *curveRow = new QHBoxLayout;
    curveRow->addWidget(curve, 1);
    curveRow->addWidget(curveLabel);
    auto *minSize = new QSpinBox(this);
    minSize->setRange(1, 100);
    minSize->setSuffix(QStringLiteral(" %"));
    minSize->setValue(s.pressureMin);
    connect(minSize, &QSpinBox::valueChanged, this, [&app, commit](int v) { app.mutableSettings().pressureMin = v; commit(); });
    form->addRow(size);
    form->addRow(opacity);
    form->addRow(QStringLiteral("Pressure curve:"), curveRow);
    form->addRow(QStringLiteral("Minimum brush size:"), minSize);
    form->addRow(new QLabel(QStringLiteral("Applies to Paintbrush, Eraser, Clone Stamp and Recolor. A mouse always draws at full pressure."), this));
    return titled(this, QStringLiteral("Tools & Tablet"), form);
}

// ------------------------------------------------------------ Autosave

QWidget *SettingsDialog::autosavePage()
{
    auto *form = new QFormLayout;
    AutosaveManager *as = m_window->autosave();
    auto *enabled = new QCheckBox(QStringLiteral("Automatically save recovery copies of changed images"), this);
    enabled->setChecked(as->isEnabled());
    auto *minutes = new QSpinBox(this);
    minutes->setRange(1, 120);
    minutes->setSuffix(QStringLiteral(" minutes"));
    minutes->setValue(as->intervalMinutes());
    minutes->setEnabled(enabled->isChecked());
    connect(enabled, &QCheckBox::toggled, this, [as, minutes](bool on) {
        as->setEnabled(on);
        minutes->setEnabled(on);
        QSettings().setValue(QStringLiteral("Autosave/enabled"), on);
    });
    connect(minutes, &QSpinBox::valueChanged, this, [as](int v) {
        as->setIntervalMinutes(v);
        QSettings().setValue(QStringLiteral("Autosave/minutes"), v);
    });
    auto *folder = new QPushButton(QStringLiteral("Open recovery folder"), this);
    connect(folder, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(AutosaveManager::recoveryDir())); });
    form->addRow(enabled);
    form->addRow(QStringLiteral("Every:"), minutes);
    form->addRow(new QLabel(QStringLiteral("Recovery copies are layered and are deleted when you save or close the image.\n"
                                           "If Pane closes unexpectedly, it offers them the next time it starts."),
                            this));
    form->addRow(folder);
    return titled(this, QStringLiteral("Autosave"), form);
}

// ------------------------------------------------------------ Keyboard

QWidget *SettingsDialog::keyboardPage()
{
    auto *v = new QVBoxLayout;
    m_keyFilter = new QLineEdit(this);
    m_keyFilter->setPlaceholderText(QStringLiteral("Search commands…"));
    m_keyFilter->setClearButtonEnabled(true);
    v->addWidget(m_keyFilter);
    m_keys = new QTreeWidget(this);
    m_keys->setColumnCount(2);
    m_keys->setHeaderLabels({QStringLiteral("Command"), QStringLiteral("Shortcut")});
    m_keys->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_keys->setRootIsDecorated(true);
    v->addWidget(m_keys, 1);
    auto *row = new QHBoxLayout;
    m_keyEdit = new QKeySequenceEdit(this);
    m_keyEdit->setMaximumSequenceLength(1);
    auto *clear = new QPushButton(QStringLiteral("Clear"), this);
    auto *reset = new QPushButton(QStringLiteral("Reset"), this);
    auto *resetAll = new QPushButton(QStringLiteral("Reset All"), this);
    row->addWidget(new QLabel(QStringLiteral("Shortcut:"), this));
    row->addWidget(m_keyEdit, 1);
    row->addWidget(clear);
    row->addWidget(reset);
    row->addWidget(resetAll);
    v->addLayout(row);
    m_keyStatus = new QLabel(this);
    m_keyStatus->setWordWrap(true);
    v->addWidget(m_keyStatus);

    connect(m_keyFilter, &QLineEdit::textChanged, this, &SettingsDialog::refreshKeyboard);
    connect(m_keys, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *it) {
        QSignalBlocker b(m_keyEdit);
        m_keyEdit->setKeySequence(it && !it->data(0, RoleId).isNull() ? QKeySequence(it->text(1)) : QKeySequence());
        m_keyEdit->setEnabled(it && !it->data(0, RoleId).isNull());
        m_keyStatus->clear();
    });
    connect(m_keyEdit, &QKeySequenceEdit::editingFinished, this, [this] { applyKey(m_keyEdit->keySequence()); });
    connect(clear, &QPushButton::clicked, this, [this] { applyKey({}); });
    connect(reset, &QPushButton::clicked, this, [this] {
        QTreeWidgetItem *it = m_keys->currentItem();
        if (!it || it->data(0, RoleId).isNull()) return;
        const QString id = it->data(0, RoleId).toString();
        if (it->data(0, RoleIsTool).toBool()) {
            ToolRegistry::instance().setKey(id, QChar());
            m_window->rebuildToolShortcuts();
        } else {
            m_window->resetActionShortcut(id);
        }
        refreshKeyboard();
    });
    connect(resetAll, &QPushButton::clicked, this, [this] {
        for (const auto &s : m_window->shortcutList()) m_window->resetActionShortcut(s.id);
        for (Tool *t : ToolRegistry::instance().tools()) ToolRegistry::instance().setKey(t->id(), QChar());
        m_window->rebuildToolShortcuts();
        refreshKeyboard();
    });
    refreshKeyboard();
    return titled(this, QStringLiteral("Keyboard"), v, true);
}

void SettingsDialog::refreshKeyboard()
{
    const QString filter = m_keyFilter ? m_keyFilter->text().trimmed() : QString();
    QString selected = m_keys->currentItem() ? m_keys->currentItem()->data(0, RoleId).toString() : QString();
    m_keys->clear();
    auto *commands = new QTreeWidgetItem(m_keys, {QStringLiteral("Commands")});
    auto *tools = new QTreeWidgetItem(m_keys, {QStringLiteral("Tools (single letter; tools sharing a letter cycle)")});
    for (const auto &s : m_window->shortcutList()) {
        if (!filter.isEmpty() && !s.text.contains(filter, Qt::CaseInsensitive)) continue;
        auto *it = new QTreeWidgetItem(commands, {s.text, s.current.toString(QKeySequence::NativeText)});
        it->setData(0, RoleId, s.id);
        it->setData(0, RoleIsTool, false);
        if (s.current != s.defaultSequence) {
            QFont f = it->font(1);
            f.setBold(true);
            it->setFont(1, f);
        }
        if (s.id == selected) m_keys->setCurrentItem(it);
    }
    for (Tool *t : ToolRegistry::instance().tools()) {
        if (!t->enabled()) continue;
        if (!filter.isEmpty() && !t->name().contains(filter, Qt::CaseInsensitive)) continue;
        QChar k = ToolRegistry::instance().keyFor(t);
        auto *it = new QTreeWidgetItem(tools, {t->name(), k.isNull() ? QString() : QString(k)});
        it->setData(0, RoleId, t->id());
        it->setData(0, RoleIsTool, true);
        if (k != t->shortcut()) {
            QFont f = it->font(1);
            f.setBold(true);
            it->setFont(1, f);
        }
        if (t->id() == selected) m_keys->setCurrentItem(it);
    }
    m_keys->expandAll();
}

void SettingsDialog::applyKey(const QKeySequence &seq)
{
    QTreeWidgetItem *it = m_keys->currentItem();
    if (!it || it->data(0, RoleId).isNull()) return;
    const QString id = it->data(0, RoleId).toString();
    const QString text = seq.toString(QKeySequence::NativeText);
    QStringList conflicts;
    if (it->data(0, RoleIsTool).toBool()) {
        // Tool keys are a single unmodified letter.
        QChar key;
        if (!seq.isEmpty()) {
            QKeyCombination kc = seq[0];
            if (kc.keyboardModifiers() != Qt::NoModifier || kc.key() < Qt::Key_A || kc.key() > Qt::Key_Z) {
                m_keyStatus->setText(QStringLiteral("Tool shortcuts must be a single letter without modifiers."));
                return;
            }
            key = QChar(char16_t(kc.key()));
        }
        ToolRegistry::instance().setKey(id, key); // an empty sequence restores the default letter
        m_window->rebuildToolShortcuts();
        for (const auto &s : m_window->shortcutList())
            if (!key.isNull() && s.current == QKeySequence(QString(key))) conflicts << s.text;
    } else {
        m_window->setActionShortcut(id, seq);
        if (!seq.isEmpty()) {
            for (const auto &s : m_window->shortcutList())
                if (s.id != id && s.current == seq) conflicts << s.text;
            if (seq[0].keyboardModifiers() == Qt::NoModifier && seq[0].key() >= Qt::Key_A && seq[0].key() <= Qt::Key_Z)
                for (Tool *t : ToolRegistry::instance().tools())
                    if (t->enabled() && ToolRegistry::instance().keyFor(t) == QChar(char16_t(seq[0].key()))) conflicts << t->name();
        }
    }
    refreshKeyboard();
    m_keyStatus->setText(conflicts.isEmpty() ? QString()
                                             : QStringLiteral("⚠ %1 is also used by: %2").arg(text, conflicts.join(QStringLiteral(", "))));
}

// ------------------------------------------------------------ Diagnostics

QWidget *SettingsDialog::diagnosticsPage()
{
    auto *v = new QVBoxLayout;
    auto *text = new QPlainTextEdit(this);
    text->setReadOnly(true);
    QStringList lines;
    lines << QStringLiteral("Pane %1").arg(QCoreApplication::applicationVersion())
          << QStringLiteral("Qt %1 (%2)").arg(QString::fromLatin1(qVersion()), QGuiApplication::platformName())
          << QStringLiteral("Threads: %1 (pool %2)").arg(QThread::idealThreadCount()).arg(QThreadPool::globalInstance()->maxThreadCount())
          << QStringLiteral("Device pixel ratio: %1").arg(devicePixelRatioF())
          << QStringLiteral("Theme: %1").arg(ThemeEngine::instance().current().name)
          << QStringLiteral("Settings: %1").arg(QSettings().fileName())
          << QStringLiteral("Recovery folder: %1").arg(AutosaveManager::recoveryDir());
    text->setPlainText(lines.join(QLatin1Char('\n')));
    v->addWidget(text);
    auto *copy = new QPushButton(QStringLiteral("Copy to clipboard"), this);
    connect(copy, &QPushButton::clicked, text, [text] {
        text->selectAll();
        text->copy();
    });
    v->addWidget(copy, 0, Qt::AlignLeft);
    return titled(this, QStringLiteral("Diagnostics"), v, true);
}

} // namespace pane
