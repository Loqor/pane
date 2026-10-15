// UI smoke tests (QT_QPA_PLATFORM=offscreen). Set PANE_SCREENSHOT_DIR to save
// window grabs for each theme.
#include "core/Commands.h"
#include "core/Glyphs.h"
#include "tools/DrawTools.h"
#include "tools/Tools.h"
#include "ui/CanvasView.h"
#include "ui/MainWindow.h"
#include "ui/theme/Theme.h"
#include "effects/Effect.h"
#include "io/FileType.h"
#include "ui/dialogs/Dialogs.h"
#include "ui/dialogs/SettingsDialog.h"
#include "ui/Autosave.h"

#include <QComboBox>
#include <QJsonDocument>
#include <QJsonObject>

#include <QLabel>
#include <QRegularExpression>

#include <QApplication>
#include <QDialog>
#include <QTimer>

#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>

using namespace pane;

class UiSmokeTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("pane-test"));
        QCoreApplication::setApplicationName(QStringLiteral("pane-test"));
        QSettings().clear();
        ThemeEngine::instance().apply(QStringLiteral("light"));
    }

    void strokeAndUndo()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.newImage(QSize(640, 480));
        QCOMPARE(w.workspaceCount(), 1);
        Document &doc = *w.current()->doc;
        w.setActiveTool(QStringLiteral("Paintbrush"));
        AppState::instance().setPrimary(0xFFFF0000);
        AppState::instance().mutableSettings().brushWidth = 20;

        QWidget *vp = w.canvas()->viewport();
        QPoint a = w.canvas()->imageToView().map(QPointF(100, 100)).toPoint();
        QPoint b = w.canvas()->imageToView().map(QPointF(300, 200)).toPoint();
        QTest::mousePress(vp, Qt::LeftButton, {}, a);
        for (int i = 1; i <= 10; ++i) QTest::mouseMove(vp, a + (b - a) * i / 10);
        QTest::mouseRelease(vp, Qt::LeftButton, {}, b);
        QCOMPARE(doc.layer(0).surface.pixel(200, 150), 0xFFFF0000u);
        QCOMPARE(doc.history().count(), 2);

        w.action(QStringLiteral("edit.undo"))->trigger();
        QCOMPARE(doc.layer(0).surface.pixel(200, 150), 0xFFFFFFFFu);
        w.action(QStringLiteral("edit.redo"))->trigger();
        QCOMPARE(doc.layer(0).surface.pixel(200, 150), 0xFFFF0000u);

        // Every enabled tool can be activated and receive a click.
        for (Tool *t : ToolRegistry::instance().tools()) {
            if (!t->enabled()) continue;
            w.setActiveTool(t->id());
            QCOMPARE(w.canvas()->tool(), t);
            QTest::mouseClick(vp, Qt::LeftButton, {}, a);
        }
        w.setActiveTool(QStringLiteral("Paintbrush"));

        // Layers + selection commands.
        w.action(QStringLiteral("layer.add"))->trigger();
        QCOMPARE(doc.layerCount(), 2);
        w.action(QStringLiteral("edit.selectAll"))->trigger();
        QVERIFY(!doc.selection().isEmpty());
        w.action(QStringLiteral("edit.deselect"))->trigger();
        QVERIFY(doc.selection().isEmpty());
        w.action(QStringLiteral("image.flatten"))->trigger();
        QCOMPARE(doc.layerCount(), 1);

        // Panels toggle with F5-F8.
        for (int key : {Qt::Key_F5, Qt::Key_F6, Qt::Key_F7, Qt::Key_F8}) {
            QTest::keyClick(&w, Qt::Key(key));
            QTest::keyClick(&w, Qt::Key(key));
        }
        doc.markClean();
    }

    void m2Tools()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.newImage(QSize(800, 600));
        Document &doc = *w.current()->doc;
        CanvasView *cv = w.canvas();
        QWidget *vp = cv->viewport();
        auto at = [cv](double x, double y) { return cv->imageToView().map(QPointF(x, y)).toPoint(); };
        auto drag = [&](QPoint a, QPoint b, Qt::MouseButton btn = Qt::LeftButton) {
            QTest::mousePress(vp, btn, {}, a);
            for (int i = 1; i <= 8; ++i) QTest::mouseMove(vp, a + (b - a) * i / 8);
            QTest::mouseRelease(vp, btn, {}, b);
        };
        AppState &app = AppState::instance();
        app.setPrimary(0xFF1E66F5);
        app.setSecondary(0xFFFFFFFF);
        int items = doc.history().count();

        // Gradient: drag, then Enter commits one history item.
        w.setActiveTool(QStringLiteral("Gradient"));
        drag(at(0, 0), at(800, 0));
        QTest::keyClick(cv, Qt::Key_Return);
        QCOMPARE(doc.history().count(), ++items);
        QVERIFY(pxB(doc.layer(0).surface.pixel(5, 300)) > 200);

        // Shapes: draw a filled-with-outline star.
        w.setActiveTool(QStringLiteral("Shapes"));
        app.mutableSettings().shapeType = int(ShapeType::Star);
        app.mutableSettings().shapeStyle = 2;
        app.mutableSettings().brushWidth = 4;
        drag(at(80, 80), at(300, 280));
        QTest::keyClick(cv, Qt::Key_Return);
        QCOMPARE(doc.history().count(), ++items);

        // Line / curve with an inserted control point.
        w.setActiveTool(QStringLiteral("LineCurve"));
        drag(at(400, 500), at(750, 500));
        drag(at(575, 500), at(575, 380));
        app.mutableSettings().arrowEnd = true;
        app.notifySettingsChanged();
        QTest::keyClick(cv, Qt::Key_Return);
        QCOMPARE(doc.history().count(), ++items);

        // Text: typing letters must not switch tools.
        w.setActiveTool(QStringLiteral("Text"));
        app.mutableSettings().fontSize = 36;
        app.mutableSettings().bold = true;
        QTest::mouseClick(vp, Qt::LeftButton, {}, at(420, 120));
        QTest::keyClicks(cv, QStringLiteral("Pane M2"));
        QCOMPARE(w.canvas()->tool()->id(), QStringLiteral("Text"));
        QTest::keyClick(cv, Qt::Key_Escape);
        QCOMPARE(doc.history().count(), ++items);

        // Ellipse select, magic wand, lasso.
        w.setActiveTool(QStringLiteral("EllipseSelect"));
        drag(at(500, 250), at(700, 350));
        QVERIFY(!doc.selection().isEmpty());
        QCOMPARE(doc.history().count(), ++items);
        w.setActiveTool(QStringLiteral("MagicWand"));
        QTest::mouseClick(vp, Qt::LeftButton, {}, at(790, 590));
        QVERIFY(!doc.selection().isEmpty());
        QCOMPARE(doc.history().count(), ++items);
        w.setActiveTool(QStringLiteral("LassoSelect"));
        QTest::mousePress(vp, Qt::LeftButton, {}, at(100, 400));
        QTest::mouseMove(vp, at(200, 420));
        QTest::mouseMove(vp, at(150, 560));
        QTest::mouseMove(vp, at(60, 500));
        QTest::mouseRelease(vp, Qt::LeftButton, {}, at(60, 500));
        QVERIFY(!doc.selection().isEmpty());
        QCOMPARE(doc.history().count(), ++items);
        w.action(QStringLiteral("edit.deselect"))->trigger();
        ++items;

        // Clone stamp and recolor.
        w.setActiveTool(QStringLiteral("CloneStamp"));
        QTest::mouseClick(vp, Qt::LeftButton, Qt::ControlModifier, at(190, 180));
        drag(at(60, 520), at(120, 560));
        QCOMPARE(doc.history().count(), ++items);
        w.setActiveTool(QStringLiteral("Recolor"));
        app.mutableSettings().brushWidth = 40;
        drag(at(500, 50), at(700, 50));
        QCOMPARE(doc.history().count(), ++items);

        QString dir = qEnvironmentVariable("PANE_SCREENSHOT_DIR");
        QTest::qWait(30);
        if (!dir.isEmpty()) w.grab().toImage().save(QDir(dir).filePath(QStringLiteral("pane-m2.png")));

        // Undo everything back to the start.
        doc.history().jumpTo(0);
        QCOMPARE(doc.layer(0).surface.pixel(5, 300), 0xFFFFFFFFu);
        app.mutableSettings() = ToolSettings();
        doc.markClean();
    }

    void m3Adjustments()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.newImage(QSize(640, 480));
        Document &doc = *w.current()->doc;
        CoverageStroke s(doc, 0, 0xFF3366CC, PaintMode::Blend);
        s.addLine({50, 50}, {600, 400}, 60, 0.5, true, 4);
        doc.history().push(s.finish(QStringLiteral("Paintbrush"), glyphs::Brush));
        QString dir = qEnvironmentVariable("PANE_SCREENSHOT_DIR");
        int items = doc.history().count();
        for (const Effect *fx : EffectRegistry::instance().effects()) {
            if (fx->category() != QLatin1String("Adjustments")) continue;
            bool hasDialog = fx->ui() != Effect::UI::None;
            if (hasDialog) {
                QTimer::singleShot(150, [&, fx] {
                    auto *dlg = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                    QVERIFY(dlg);
                    if (!dir.isEmpty() && (fx->id() == QLatin1String("Curves") || fx->id() == QLatin1String("Levels") ||
                                           fx->id() == QLatin1String("HueSaturation")))
                        dlg->grab().toImage().save(QDir(dir).filePath(QStringLiteral("dlg-%1.png").arg(fx->id())));
                    dlg->accept();
                });
            }
            w.action(QStringLiteral("effect.") + fx->id())->trigger();
            // Identity-parameter dialogs may produce no change; non-dialog ones must.
            if (!hasDialog) QCOMPARE(doc.history().count(), ++items);
            items = doc.history().count();
        }
        QVERIFY(doc.history().count() >= 6);
        doc.history().jumpTo(1);
        doc.markClean();
    }

    void m4Effects()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.newImage(QSize(400, 300));
        Document &doc = *w.current()->doc;
        QString dir = qEnvironmentVariable("PANE_SCREENSHOT_DIR");
        QVERIFY(!w.action(QStringLiteral("effect.repeat"))->isEnabled());
        int opened = 0;
        for (const Effect *fx : EffectRegistry::instance().effects()) {
            if (fx->category() == QLatin1String("Adjustments")) continue;
            QTimer::singleShot(100, [&, fx] {
                auto *dlg = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                QVERIFY2(dlg, qPrintable(fx->id()));
                if (!dir.isEmpty() && (fx->id() == QLatin1String("DropShadow") || fx->id() == QLatin1String("Clouds")))
                    dlg->grab().toImage().save(QDir(dir).filePath(QStringLiteral("dlg-%1.png").arg(fx->id())));
                dlg->accept();
            });
            w.action(QStringLiteral("effect.") + fx->id())->trigger();
            ++opened;
        }
        QCOMPARE(opened, 41);
        QVERIFY(w.action(QStringLiteral("effect.repeat"))->isEnabled());
        int before = doc.history().count();
        w.action(QStringLiteral("effect.repeat"))->trigger(); // no dialog
        QCOMPARE(doc.history().count(), before + 1);
        // Clouds with defaults renders a visible result for the screenshot.
        QTimer::singleShot(100, [] { qobject_cast<QDialog *>(QApplication::activeModalWidget())->accept(); });
        w.action(QStringLiteral("effect.Clouds"))->trigger();
        QTest::qWait(50);
        if (!dir.isEmpty()) w.grab().toImage().save(QDir(dir).filePath(QStringLiteral("pane-m4.png")));
        doc.markClean();
    }

    void saveConfigDialogs()
    {
        auto doc = Document::createBlank(QSize(320, 200), 0xFF3366CCu);
        QString dir = qEnvironmentVariable("PANE_SCREENSHOT_DIR");
        for (const FileType *t : filetypes::all()) {
            if (!t->hasSaveOptions() || !t->canSave()) continue;
            SaveConfigDialog dlg(t, *doc, doc->flattenToImage(), propertyDefaults(t->saveOptions()));
            dlg.show();
            QLabel *size = nullptr;
            for (QLabel *l : dlg.findChildren<QLabel *>())
                if (l->text() == QStringLiteral("Calculating…")) size = l;
            QVERIFY2(size, qPrintable(t->id()));
            QTRY_VERIFY2_WITH_TIMEOUT(size->text() != QStringLiteral("Calculating…"), qPrintable(t->id()), 20000);
            QVERIFY2(size->text().contains(QRegularExpression(QStringLiteral("[0-9]"))), qPrintable(t->id() + ": " + size->text()));
            if (!dir.isEmpty() && t->id() == QLatin1String("avif")) {
                QTest::qWait(100);
                dlg.grab().toImage().save(QDir(dir).filePath(QStringLiteral("dlg-save-avif.png")));
            }
        }
    }

    void settingsAndShortcuts()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.newImage(QSize(200, 150));
        {
            SettingsDialog dlg(&w);
            dlg.show();
            for (int i = 0; i < SettingsDialog::pageNames().size(); ++i) dlg.showPage(i);
            // Theme change from the dialog applies immediately.
            auto *theme = dlg.findChildren<QComboBox *>().first();
            theme->setCurrentIndex(theme->findData(QStringLiteral("mocha")));
            QCOMPARE(ThemeEngine::instance().currentId(), QStringLiteral("mocha"));
            QString dir = qEnvironmentVariable("PANE_SCREENSHOT_DIR");
            dlg.showPage(3);
            QTest::qWait(30);
            if (!dir.isEmpty()) dlg.grab().toImage().save(QDir(dir).filePath(QStringLiteral("dlg-settings-keyboard.png")));
            ThemeEngine::instance().apply(QStringLiteral("light"));
        }
        w.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&w));
        // Remap Flatten to Ctrl+Alt+F and trigger it from the keyboard.
        Document &doc = *w.current()->doc;
        w.action(QStringLiteral("layer.add"))->trigger();
        QCOMPARE(doc.layerCount(), 2);
        w.setActionShortcut(QStringLiteral("image.flatten"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F));
        QTest::keyClick(&w, Qt::Key_F, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(doc.layerCount(), 1);
        QCOMPARE(QSettings().value(QStringLiteral("Shortcuts/image.flatten")).toString(), QStringLiteral("Ctrl+Alt+F"));
        w.resetActionShortcut(QStringLiteral("image.flatten"));
        QCOMPARE(w.action(QStringLiteral("image.flatten"))->shortcut(), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F));
        // Remap the Paintbrush to "N" (no other tool uses it).
        ToolRegistry::instance().setKey(QStringLiteral("Paintbrush"), QLatin1Char('N'));
        w.rebuildToolShortcuts();
        w.setActiveTool(QStringLiteral("Pencil"));
        QTest::keyClick(&w, Qt::Key_N);
        QCOMPARE(w.canvas()->tool()->id(), QStringLiteral("Paintbrush"));
        ToolRegistry::instance().setKey(QStringLiteral("Paintbrush"), QChar());
        w.rebuildToolShortcuts();
        doc.markClean();
    }

    void autosaveAndRecovery()
    {
        QDir(AutosaveManager::recoveryDir()).removeRecursively();
        MainWindow w;
        w.newImage(QSize(300, 200));
        Document &doc = *w.current()->doc;
        commands::addNewLayer(doc);
        doc.layer(1).surface.fillRect(QRect(10, 10, 50, 50), 0xFFFF00FFu);
        LayerProperties p = doc.layer(1).props;
        p.blendMode = BlendMode::Screen;
        doc.setLayerProperties(1, p);
        QVERIFY(doc.isDirty());
        w.autosave()->autosaveNow();
        w.autosave()->waitForPending();
        // Our own entries are not orphans while we are running.
        QVERIFY(AutosaveManager::orphans().empty());
        // Simulate a crash: the owning process no longer exists.
        QDir rec(AutosaveManager::recoveryDir());
        const QStringList jsons = rec.entryList({QStringLiteral("*.json")});
        QCOMPARE(jsons.size(), 1);
        QFile jf(rec.filePath(jsons.first()));
        QVERIFY(jf.open(QIODevice::ReadWrite));
        QJsonObject o = QJsonDocument::fromJson(jf.readAll()).object();
        o[QStringLiteral("pid")] = 999999;
        jf.resize(0);
        jf.write(QJsonDocument(o).toJson());
        jf.close();
        auto orphans = AutosaveManager::orphans();
        QCOMPARE(int(orphans.size()), 1);
        auto back = AutosaveManager::recover(orphans.front());
        QVERIFY(back->isDirty());
        QCOMPARE(back->layerCount(), 2);
        QCOMPARE(back->layer(1).props.blendMode, BlendMode::Screen);
        QVERIFY(back->flattenToImage() == doc.flattenToImage());
        AutosaveManager::discard(orphans.front());
        QVERIFY(AutosaveManager::orphans().empty());
        // A further edit autosaves again; a clean (saved) document drops its copy.
        commands::addNewLayer(doc);
        w.autosave()->autosaveNow();
        w.autosave()->waitForPending();
        QCOMPARE(rec.entryList({QStringLiteral("*.ora")}).size(), 1);
        doc.markClean();
        w.autosave()->autosaveNow(); // clean docs drop their recovery copy
        QCOMPARE(rec.entryList({QStringLiteral("*.ora")}).size(), 0);
    }

    void panning()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        for (QSize sz : {QSize(4000, 3000), QSize(800, 600)}) {
            w.newImage(sz);
            CanvasView *cv = w.canvas();
            QWidget *vp = cv->viewport();
            auto origin = [cv] { return QPointF(cv->imageToView().dx(), cv->imageToView().dy()); };
            auto middleDrag = [&](QPoint a, QPoint b) {
                QTest::mousePress(vp, Qt::MiddleButton, {}, a);
                QMouseEvent mv(QEvent::MouseMove, QPointF(b), vp->mapToGlobal(QPointF(b)), Qt::NoButton, Qt::MiddleButton, {});
                QApplication::sendEvent(vp, &mv);
                QTest::mouseRelease(vp, Qt::MiddleButton, {}, b);
            };
            qInfo("%dx%d zoom %.3f origin (%.0f, %.0f)", sz.width(), sz.height(), cv->zoom(), origin().x(), origin().y());
            QPointF before = origin();
            middleDrag(QPoint(500, 400), QPoint(300, 250));
            qInfo("  after middle drag: (%.0f, %.0f)", origin().x(), origin().y());
            QCOMPARE(origin(), before + QPointF(-200, -150));
            before = origin();
            w.setActiveTool(QStringLiteral("Pan"));
            QTest::mousePress(vp, Qt::LeftButton, {}, QPoint(300, 250));
            QTest::mouseMove(vp, QPoint(400, 300));
            QTest::mouseRelease(vp, Qt::LeftButton, {}, QPoint(400, 300));
            qInfo("  after pan tool: (%.0f, %.0f)", origin().x(), origin().y());
            QCOMPARE(origin(), before + QPointF(100, 50));
            w.current()->doc->markClean();
        }
    }

    void themesAndScreenshots()
    {
        MainWindow w;
        w.resize(1280, 820);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.newImage(QSize(800, 600));
        Document &doc = *w.current()->doc;
        CoverageStroke s(doc, 0, 0xFF3366CC, PaintMode::Blend);
        s.addLine({100, 100}, {700, 500}, 30, 0.75, true, 4);
        s.addLine({100, 500}, {700, 100}, 12, 1.0, true, 2);
        doc.notifyPixelsChanged(s.takeDirty());
        doc.history().push(s.finish(QStringLiteral("Paintbrush"), glyphs::Brush));
        commands::addNewLayer(doc);
        Selection sel = doc.selection();
        QPainterPath p;
        p.addRect(200, 150, 300, 200);
        sel.setPath(p, false);
        doc.setSelection(sel);
        QTest::qWait(50);
        QString dir = qEnvironmentVariable("PANE_SCREENSHOT_DIR");
        for (const QString &id : ThemeEngine::instance().themeIds()) {
            ThemeEngine::instance().apply(id, id == QLatin1String("mocha") ? QStringLiteral("mauve") : QString());
            QTest::qWait(30);
            QImage shot = w.grab().toImage();
            QVERIFY(!shot.isNull());
            if (!dir.isEmpty()) shot.save(QDir(dir).filePath(QStringLiteral("pane-%1.png").arg(id)));
        }
        ThemeEngine::instance().apply(QStringLiteral("light"));
        doc.markClean();
    }
};

QTEST_MAIN(UiSmokeTest)
#include "test_ui.moc"
