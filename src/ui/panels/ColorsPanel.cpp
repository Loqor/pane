// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#include "core/Glyphs.h"
#include "core/PaletteFile.h"
#include "io/FileType.h"
#include "ui/panels/Panels.h"
#include "ui/theme/Theme.h"

#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

namespace pane {

namespace {

void drawChecker(QPainter &p, const QRect &r)
{
    const Theme &th = ThemeEngine::instance().current();
    for (int y = 0; y < r.height(); y += 5)
        for (int x = 0; x < r.width(); x += 5)
            p.fillRect(QRect(r.left() + x, r.top() + y, 5, 5).intersected(r), ((x + y) / 5) % 2 ? th.checkerB() : th.checkerA());
}

void drawSwatch(QPainter &p, const QRect &r, Pixel c)
{
    drawChecker(p, r);
    p.fillRect(r, qcolorFromPixel(c));
}

} // namespace

// ------------------------------------------------------------ ColorWheel

ColorWheel::ColorWheel(QWidget *parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
}

void ColorWheel::setHsv(double h, double s, double v)
{
    m_h = h;
    m_s = s;
    m_v = v;
    update();
}

void ColorWheel::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    int side = std::min(width(), height());
    QRect r((width() - side) / 2, (height() - side) / 2, side, side);
    double dpr = devicePixelRatioF();
    if (m_cache.isNull() || m_cacheV != m_v || m_cache.width() != int(side * dpr)) {
        int n = int(side * dpr);
        m_cache = QImage(n, n, QImage::Format_ARGB32_Premultiplied);
        m_cache.fill(0);
        double rad = n / 2.0;
        for (int y = 0; y < n; ++y) {
            auto *row = reinterpret_cast<QRgb *>(m_cache.scanLine(y));
            for (int x = 0; x < n; ++x) {
                double dx = x + 0.5 - rad, dy = y + 0.5 - rad;
                double d = std::sqrt(dx * dx + dy * dy);
                if (d > rad) continue;
                double hue = std::fmod(std::atan2(-dy, dx) * 180.0 / M_PI + 360.0, 360.0);
                QColor c = QColor::fromHsvF(float(hue / 360.0), float(std::min(1.0, d / (rad - 1))), float(m_v));
                double edge = std::clamp(rad - d, 0.0, 1.0);
                row[x] = qPremultiply(qRgba(c.red(), c.green(), c.blue(), int(255 * edge)));
            }
        }
        m_cache.setDevicePixelRatio(dpr);
        m_cacheV = m_v;
    }
    p.drawImage(r.topLeft(), m_cache);
    // Marker.
    double rad = side / 2.0;
    double a = m_h * M_PI / 180.0;
    QPointF c(r.center().x() + 0.5 + std::cos(a) * m_s * rad, r.center().y() + 0.5 - std::sin(a) * m_s * rad);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(Qt::black, 2));
    p.drawEllipse(c, 5, 5);
    p.setPen(QPen(Qt::white, 1.5));
    p.drawEllipse(c, 5, 5);
}

void ColorWheel::pick(QPointF pos)
{
    int side = std::min(width(), height());
    QPointF center(width() / 2.0, height() / 2.0);
    QPointF d = pos - center;
    double rad = side / 2.0;
    m_s = std::min(1.0, std::hypot(d.x(), d.y()) / rad);
    m_h = std::fmod(std::atan2(-d.y(), d.x()) * 180.0 / M_PI + 360.0, 360.0);
    update();
    emit hueSatChanged(m_h, m_s);
}

void ColorWheel::mousePressEvent(QMouseEvent *e) { pick(e->position()); }
void ColorWheel::mouseMoveEvent(QMouseEvent *e)
{
    if (e->buttons() & Qt::LeftButton) pick(e->position());
}

// ------------------------------------------------------------ ColorWell

ColorWell::ColorWell(QWidget *parent) : QWidget(parent)
{
    setFixedSize(64, 64);
    connect(&AppState::instance(), &AppState::colorsChanged, this, qOverload<>(&QWidget::update));
    connect(&ThemeEngine::instance(), &ThemeEngine::themeChanged, this, qOverload<>(&QWidget::update));
    setToolTip(QStringLiteral("Click to choose which color to edit.\nX swaps the colors, D resets them to black and white."));
}

void ColorWell::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const Theme &th = ThemeEngine::instance().current();
    AppState &a = AppState::instance();
    auto frame = [&](const QRect &r, bool active) {
        p.setPen(QPen(active ? th.accent : th.overlay1, active ? 2 : 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(r.adjusted(0, 0, -1, -1));
    };
    drawSwatch(p, secondaryRect(), a.secondary());
    frame(secondaryRect(), m_secondary);
    drawSwatch(p, primaryRect(), a.primary());
    frame(primaryRect(), !m_secondary);
    p.setFont(ThemeEngine::instance().glyphFont(14));
    p.setPen(th.subtext0);
    p.drawText(swapRect(), Qt::AlignCenter, glyphs::Swap);
    p.drawText(resetRect(), Qt::AlignCenter, glyphs::Reset);
}

void ColorWell::mousePressEvent(QMouseEvent *e)
{
    QPoint pt = e->position().toPoint();
    if (swapRect().contains(pt)) AppState::instance().swapColors();
    else if (resetRect().contains(pt)) AppState::instance().resetColors();
    else if (primaryRect().contains(pt)) m_secondary = false;
    else if (secondaryRect().contains(pt)) m_secondary = true;
    else return;
    update();
    emit targetChanged(m_secondary);
}

// ------------------------------------------------------------ Palette

std::vector<Pixel> PaletteGrid::defaultPalette()
{
    // 96 colors: a grayscale row, then hue rows at varying saturation/value.
    std::vector<Pixel> v;
    for (int i = 0; i < 16; ++i) {
        int g = 255 - i * 255 / 15;
        v.push_back(makePixel(g, g, g, 255));
    }
    const double sv[5][2] = {{1.0, 1.0}, {0.5, 1.0}, {1.0, 0.75}, {1.0, 0.5}, {0.25, 1.0}};
    for (auto &row : sv)
        for (int i = 0; i < 16; ++i) v.push_back(pixelFromQColor(QColor::fromHsvF(float(i / 16.0), float(row[0]), float(row[1]))));
    return v;
}

PaletteGrid::PaletteGrid(QWidget *parent) : QWidget(parent), m_colors(defaultPalette())
{
    const QStringList saved = QSettings().value(QStringLiteral("Palette/colors")).toStringList();
    if (!saved.isEmpty()) m_colors = parsePalette(saved.join(QLatin1Char('\n')));
    setFixedSize(sizeHint());
    setToolTip(QStringLiteral("Left click: set primary color. Right click: set secondary color.\nShift+click: replace the swatch with the primary color."));
    connect(&ThemeEngine::instance(), &ThemeEngine::themeChanged, this, qOverload<>(&QWidget::update));
}

void PaletteGrid::setColors(const std::vector<Pixel> &colors)
{
    m_colors = parsePalette(formatPalette(colors)); // normalizes to 96 entries
    QStringList hex;
    for (Pixel p : m_colors) hex << QStringLiteral("%1").arg(p, 8, 16, QLatin1Char('0'));
    QSettings().setValue(QStringLiteral("Palette/colors"), hex);
    update();
}

QSize PaletteGrid::sizeHint() const
{
    int rows = int((m_colors.size() + Cols - 1) / Cols);
    return {Cols * Cell + 1, rows * Cell + 1};
}

void PaletteGrid::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const Theme &th = ThemeEngine::instance().current();
    for (size_t i = 0; i < m_colors.size(); ++i) {
        QRect r(int(i % Cols) * Cell, int(i / Cols) * Cell, Cell, Cell);
        p.fillRect(r, qcolorFromPixel(m_colors[i]));
        p.setPen(th.base);
        p.drawRect(r);
    }
}

void PaletteGrid::mousePressEvent(QMouseEvent *e)
{
    int i = int(e->position().y()) / Cell * Cols + int(e->position().x()) / Cell;
    if (i < 0 || size_t(i) >= m_colors.size()) return;
    if (e->modifiers() & Qt::ShiftModifier) {
        std::vector<Pixel> c = m_colors;
        c[size_t(i)] = AppState::instance().primary();
        setColors(c);
        return;
    }
    if (e->button() == Qt::RightButton) AppState::instance().setSecondary(m_colors[size_t(i)]);
    else AppState::instance().setPrimary(m_colors[size_t(i)]);
}

// ------------------------------------------------------------ ColorsPanel

ColorsPanel::ColorsPanel(QWidget *parent) : QWidget(parent)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(2, 2, 2, 2);
    v->setSpacing(6);

    auto *top = new QHBoxLayout;
    m_well = new ColorWell(this);
    top->addWidget(m_well, 0, Qt::AlignTop);
    auto *right = new QVBoxLayout;
    m_palette = new PaletteGrid(this);
    right->addWidget(m_palette);
    auto *paletteButton = new QToolButton(this);
    paletteButton->setText(glyphs::Palette);
    paletteButton->setFont(ThemeEngine::instance().glyphFont(15));
    paletteButton->setToolTip(QStringLiteral("Palette"));
    paletteButton->setAutoRaise(true);
    paletteButton->setPopupMode(QToolButton::InstantPopup);
    auto *paletteMenu = new QMenu(paletteButton);
    paletteMenu->addAction(QStringLiteral("Open Palette..."), this, [this] {
        QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Open Palette"), QString(), QStringLiteral("Palettes (*.txt);;All files (*)"));
        if (path.isEmpty()) return;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return;
        m_palette->setColors(parsePalette(QString::fromUtf8(f.readAll())));
    });
    paletteMenu->addAction(QStringLiteral("Save Palette As..."), this, [this] {
        QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save Palette"), QStringLiteral("palette.txt"), QStringLiteral("Palettes (*.txt)"));
        if (path.isEmpty()) return;
        try {
            filetypes::writeFileAtomically(path, formatPalette(m_palette->colors()).toUtf8());
        } catch (const std::exception &e) {
            QMessageBox::critical(this, QStringLiteral("Save Palette"), QString::fromUtf8(e.what()));
        }
    });
    paletteMenu->addSeparator();
    paletteMenu->addAction(QStringLiteral("Reset to Default"), this, [this] { m_palette->setColors(PaletteGrid::defaultPalette()); });
    paletteButton->setMenu(paletteMenu);
    m_moreButton = new QToolButton(this);
    m_moreButton->setText(QStringLiteral("More »"));
    m_moreButton->setCheckable(true);
    m_moreButton->setAutoRaise(true);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(paletteButton);
    buttons->addStretch();
    buttons->addWidget(m_moreButton);
    right->addLayout(buttons);
    top->addLayout(right);
    v->addLayout(top);

    m_more = new QWidget(this);
    auto *mg = new QGridLayout(m_more);
    mg->setContentsMargins(0, 0, 0, 0);
    mg->setHorizontalSpacing(6);
    m_wheel = new ColorWheel(m_more);
    m_value = new QSlider(Qt::Vertical, m_more);
    m_value->setRange(0, 100);
    m_value->setToolTip(QStringLiteral("Value"));
    m_alpha = new QSlider(Qt::Vertical, m_more);
    m_alpha->setRange(0, 255);
    m_alpha->setToolTip(QStringLiteral("Alpha"));
    mg->addWidget(m_wheel, 0, 0, 1, 1);
    mg->addWidget(m_value, 0, 1);
    mg->addWidget(m_alpha, 0, 2);

    auto *form = new QGridLayout;
    form->setHorizontalSpacing(4);
    form->setVerticalSpacing(2);
    auto spin = [&](const QString &label, int max, int row, int col) {
        auto *l = new QLabel(label, m_more);
        auto *s = new QSpinBox(m_more);
        s->setRange(0, max);
        s->setButtonSymbols(QAbstractSpinBox::NoButtons);
        s->setFixedWidth(44);
        form->addWidget(l, row, col * 2);
        form->addWidget(s, row, col * 2 + 1);
        return s;
    };
    m_r = spin(QStringLiteral("R"), 255, 0, 0);
    m_g = spin(QStringLiteral("G"), 255, 1, 0);
    m_b = spin(QStringLiteral("B"), 255, 2, 0);
    m_hS = spin(QStringLiteral("H"), 360, 0, 1);
    m_sS = spin(QStringLiteral("S"), 100, 1, 1);
    m_vS = spin(QStringLiteral("V"), 100, 2, 1);
    m_a = spin(QStringLiteral("A"), 255, 3, 0);
    auto *hexLabel = new QLabel(QStringLiteral("Hex"), m_more);
    m_hex = new QLineEdit(m_more);
    m_hex->setMaxLength(8);
    m_hex->setFixedWidth(76);
    form->addWidget(hexLabel, 3, 2);
    form->addWidget(m_hex, 3, 3);
    mg->addLayout(form, 1, 0, 1, 3);
    v->addWidget(m_more);
    m_more->setVisible(false);

    connect(m_moreButton, &QToolButton::toggled, this, [this](bool on) {
        m_more->setVisible(on);
        m_moreButton->setText(on ? QStringLiteral("« Less") : QStringLiteral("More »"));
        if (auto *panel = parentWidget()) panel->adjustSize();
    });
    connect(m_well, &ColorWell::targetChanged, this, &ColorsPanel::syncFromState);
    connect(&AppState::instance(), &AppState::colorsChanged, this, &ColorsPanel::syncFromState);
    connect(m_wheel, &ColorWheel::hueSatChanged, this, [this](double h, double s) { setFromHsv(h, s, m_v, m_a->value()); });
    connect(m_value, &QSlider::valueChanged, this, [this](int v) { if (!m_syncing) setFromHsv(m_h, m_s, v / 100.0, m_a->value()); });
    connect(m_alpha, &QSlider::valueChanged, this, [this](int a) { if (!m_syncing) setFromRgb(m_r->value(), m_g->value(), m_b->value(), a); });
    auto rgbChanged = [this] { if (!m_syncing) setFromRgb(m_r->value(), m_g->value(), m_b->value(), m_a->value()); };
    for (QSpinBox *s : {m_r, m_g, m_b, m_a}) connect(s, &QSpinBox::valueChanged, this, rgbChanged);
    auto hsvChanged = [this] { if (!m_syncing) setFromHsv(m_hS->value() % 360, m_sS->value() / 100.0, m_vS->value() / 100.0, m_a->value()); };
    for (QSpinBox *s : {m_hS, m_sS, m_vS}) connect(s, &QSpinBox::valueChanged, this, hsvChanged);
    connect(m_hex, &QLineEdit::editingFinished, this, [this] {
        QString t = m_hex->text().trimmed().remove('#');
        bool ok = false;
        uint v = t.toUInt(&ok, 16);
        if (!ok) return syncFromState();
        if (t.size() == 6) setFromRgb(int(v >> 16) & 255, int(v >> 8) & 255, int(v) & 255, m_a->value());
        else if (t.size() == 8) setFromRgb(int(v >> 24) & 255, int(v >> 16) & 255, int(v >> 8) & 255, int(v) & 255);
        else syncFromState();
    });
    syncFromState();
}

Pixel ColorsPanel::current() const
{
    return m_well->editingSecondary() ? AppState::instance().secondary() : AppState::instance().primary();
}

void ColorsPanel::commit(Pixel p)
{
    if (m_well->editingSecondary()) AppState::instance().setSecondary(p);
    else AppState::instance().setPrimary(p);
}

void ColorsPanel::setFromHsv(double h, double s, double v, int a)
{
    m_h = h;
    m_s = s;
    m_v = v;
    QColor c = QColor::fromHsvF(float(h / 360.0), float(s), float(v));
    m_syncing = true; // keep our HSV (hue is lost at s=0) while state updates
    commit(makePixel(c.red(), c.green(), c.blue(), a));
    m_syncing = false;
    syncFromState();
}

void ColorsPanel::setFromRgb(int r, int g, int b, int a)
{
    QColor c(r, g, b);
    if (c.hsvSaturationF() > 0) m_h = c.hsvHueF() * 360.0;
    m_s = c.hsvSaturationF();
    m_v = c.valueF();
    m_syncing = true;
    commit(makePixel(r, g, b, a));
    m_syncing = false;
    syncFromState();
}

void ColorsPanel::syncFromState()
{
    if (m_syncing) return;
    m_syncing = true;
    Pixel p = current();
    QColor c = qcolorFromPixel(p);
    // Keep hue/saturation stable for grays and black.
    if (c.hsvSaturationF() > 0 && c.valueF() > 0) m_h = c.hsvHueF() * 360.0;
    if (c.valueF() > 0) m_s = c.hsvSaturationF();
    m_v = c.valueF();
    m_wheel->setHsv(m_h, m_s, m_v);
    m_value->setValue(int(std::lround(m_v * 100)));
    m_alpha->setValue(pxA(p));
    m_r->setValue(pxR(p));
    m_g->setValue(pxG(p));
    m_b->setValue(pxB(p));
    m_a->setValue(pxA(p));
    m_hS->setValue(int(std::lround(m_h)) % 360);
    m_sS->setValue(int(std::lround(m_s * 100)));
    m_vS->setValue(int(std::lround(m_v * 100)));
    if (!m_hex->hasFocus()) m_hex->setText(pixelToHex(p, pxA(p) != 255));
    m_syncing = false;
}

} // namespace pane
