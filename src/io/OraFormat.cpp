// OpenRaster (.ora): a ZIP with stack.xml, one PNG per layer, a merged image and
// a thumbnail. Keeps layers, names, opacity, visibility and blend modes.
#include "core/Glyphs.h"
#include "core/Parallel.h"
#include "core/Resample.h"
#include "io/Formats.h"
#include "io/PngWriter.h"
#include "io/Zip.h"

#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace pane {

namespace {

const std::pair<BlendMode, const char *> kOps[] = {
    {BlendMode::Normal, "svg:src-over"},     {BlendMode::Multiply, "svg:multiply"},   {BlendMode::ColorBurn, "svg:color-burn"},
    {BlendMode::ColorDodge, "svg:color-dodge"}, {BlendMode::Overlay, "svg:overlay"},  {BlendMode::Difference, "svg:difference"},
    {BlendMode::Lighten, "svg:lighten"},     {BlendMode::Darken, "svg:darken"},       {BlendMode::Screen, "svg:screen"},
    {BlendMode::Additive, "svg:plus"},       {BlendMode::Xor, "pane:xor"},            {BlendMode::Reflect, "pane:reflect"},
    {BlendMode::Glow, "pane:glow"},          {BlendMode::Negation, "pane:negation"},
};

QString opFor(BlendMode m)
{
    for (auto &[mode, op] : kOps)
        if (mode == m) return QString::fromLatin1(op);
    return QStringLiteral("svg:src-over");
}

BlendMode modeFor(const QString &op)
{
    for (auto &[mode, name] : kOps)
        if (op == QLatin1String(name)) return mode;
    return BlendMode::Normal;
}

class OraFileType final : public FileType {
public:
    QString id() const override { return QStringLiteral("ora"); }
    QString name() const override { return QStringLiteral("OpenRaster"); }
    QStringList extensions() const override { return {QStringLiteral("ora")}; }
    bool supportsLayers() const override { return true; }

    std::unique_ptr<Document> load(const QByteArray &data) const override
    {
        std::map<QString, QByteArray> files;
        try {
            files = zip::read(data);
        } catch (const std::exception &e) {
            throw FileError(std::string("This OpenRaster file could not be read: ") + e.what());
        }
        auto stackIt = files.find(QStringLiteral("stack.xml"));
        if (stackIt == files.end()) throw FileError("This OpenRaster file has no stack.xml.");
        QXmlStreamReader xml(stackIt->second);
        int w = 0, h = 0;
        double xres = 96, yres = 96;
        struct L { QString name, src; int x, y; double opacity; bool visible; BlendMode mode; };
        std::vector<L> layers; // top-most first, as in stack.xml
        while (!xml.atEnd()) {
            if (!xml.readNextStartElement()) continue;
            auto a = xml.attributes();
            if (xml.name() == QLatin1String("image")) {
                w = a.value(QStringLiteral("w")).toInt();
                h = a.value(QStringLiteral("h")).toInt();
                if (a.hasAttribute(QStringLiteral("xres"))) xres = a.value(QStringLiteral("xres")).toDouble();
                if (a.hasAttribute(QStringLiteral("yres"))) yres = a.value(QStringLiteral("yres")).toDouble();
            } else if (xml.name() == QLatin1String("layer")) {
                layers.push_back({a.value(QStringLiteral("name")).toString(), a.value(QStringLiteral("src")).toString(),
                                  a.value(QStringLiteral("x")).toInt(), a.value(QStringLiteral("y")).toInt(),
                                  a.hasAttribute(QStringLiteral("opacity")) ? a.value(QStringLiteral("opacity")).toDouble() : 1.0,
                                  a.value(QStringLiteral("visibility")) != QLatin1String("hidden"),
                                  modeFor(a.value(QStringLiteral("composite-op")).toString())});
            }
        }
        if (w <= 0 || h <= 0 || w > 65535 || h > 65535) throw FileError("This OpenRaster file has an invalid size.");
        if (layers.empty()) throw FileError("This OpenRaster file has no layers.");
        // Decode layer PNGs in parallel.
        std::vector<QImage> images(layers.size());
        parallelFor(int(layers.size()), [&](int i) {
            auto it = files.find(layers[size_t(i)].src);
            if (it == files.end()) return;
            QImage img;
            if (img.loadFromData(it->second)) images[size_t(i)] = img.convertToFormat(QImage::Format_ARGB32);
        });
        auto doc = std::make_unique<Document>(QSize(w, h));
        doc->setDpi(xres, yres);
        for (size_t k = layers.size(); k-- > 0;) { // bottom-most first
            Layer l;
            l.props.name = layers[k].name.isEmpty() ? doc->nextLayerName() : layers[k].name;
            l.props.visible = layers[k].visible;
            l.props.opacity = uint8_t(std::clamp(int(std::lround(layers[k].opacity * 255)), 0, 255));
            l.props.blendMode = layers[k].mode;
            l.surface = Surface(w, h);
            if (!images[k].isNull()) l.surface.writeImage(images[k], QPoint(layers[k].x, layers[k].y));
            l.surface.compact();
            doc->insertLayer(doc->layerCount(), std::move(l));
        }
        doc->history().reset(std::make_unique<BaseHistoryItem>(QStringLiteral("Open Image"), glyphs::Image));
        return doc;
    }

    QByteArray save(const Document &doc, const QImage &, const SaveOptions &) const override
    {
        const int n = doc.layerCount();
        std::vector<zip::Entry> entries;
        entries.push_back({QStringLiteral("mimetype"), QByteArrayLiteral("image/openraster"), false});
        // Encode layers in parallel (each encoder is itself parallel; nesting is safe).
        std::vector<QByteArray> pngs(static_cast<size_t>(n));
        parallelFor(n, [&](int i) {
            pngs[size_t(i)] = encodePngParallel(doc.layer(i).surface.toImage(), true, doc.dpiX(), doc.dpiY());
        });
        QByteArray stack;
        QXmlStreamWriter xml(&stack);
        xml.setAutoFormatting(true);
        xml.writeStartDocument();
        xml.writeStartElement(QStringLiteral("image"));
        xml.writeAttribute(QStringLiteral("version"), QStringLiteral("0.0.3"));
        xml.writeAttribute(QStringLiteral("w"), QString::number(doc.width()));
        xml.writeAttribute(QStringLiteral("h"), QString::number(doc.height()));
        xml.writeAttribute(QStringLiteral("xres"), QString::number(doc.dpiX()));
        xml.writeAttribute(QStringLiteral("yres"), QString::number(doc.dpiY()));
        xml.writeStartElement(QStringLiteral("stack"));
        for (int i = n - 1; i >= 0; --i) { // top-most first
            const LayerProperties &p = doc.layer(i).props;
            xml.writeStartElement(QStringLiteral("layer"));
            xml.writeAttribute(QStringLiteral("name"), p.name);
            xml.writeAttribute(QStringLiteral("src"), QStringLiteral("data/layer%1.png").arg(i));
            xml.writeAttribute(QStringLiteral("x"), QStringLiteral("0"));
            xml.writeAttribute(QStringLiteral("y"), QStringLiteral("0"));
            xml.writeAttribute(QStringLiteral("opacity"), QString::number(p.opacity / 255.0, 'f', 4));
            xml.writeAttribute(QStringLiteral("visibility"), p.visible ? QStringLiteral("visible") : QStringLiteral("hidden"));
            xml.writeAttribute(QStringLiteral("composite-op"), opFor(p.blendMode));
            xml.writeEndElement();
        }
        xml.writeEndElement();
        xml.writeEndElement();
        xml.writeEndDocument();
        entries.push_back({QStringLiteral("stack.xml"), stack, true});
        for (int i = 0; i < n; ++i) entries.push_back({QStringLiteral("data/layer%1.png").arg(i), pngs[size_t(i)], false});
        QImage merged = doc.flattenToImage();
        entries.push_back({QStringLiteral("mergedimage.png"), encodePngParallel(merged, true, doc.dpiX(), doc.dpiY()), false});
        QSize ts = merged.size().scaled(256, 256, Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
        QImage thumb = ts == merged.size() ? merged : resampleImage(merged, ts, Resampling::BestQuality);
        entries.push_back({QStringLiteral("Thumbnails/thumbnail.png"), encodePngParallel(thumb, true, 96, 96), false});
        return zip::write(entries);
    }
};

} // namespace

const FileType *oraFileType() { static OraFileType t; return &t; }

} // namespace pane
