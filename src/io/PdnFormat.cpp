// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

// paint.net .pdn reader.
//
// Layout: "PDN3" magic, 24-bit little-endian header length, UTF-8 XML header,
// then either 0x00 0x01 followed by a BinaryFormatter (NRBF) stream whose
// MemoryBlocks are "deferred" (their pixel data follows the stream in chunks),
// or 0x1F 0x8B: the whole remainder is gzip-compressed and blocks are inline.
#include "io/Formats.h"
#include "io/Nrbf.h"

#include "core/Glyphs.h"
#include "core/Parallel.h"
#include "core/Resample.h"
#include "io/PngWriter.h"

#include <QtEndian>
#include <cstring>
#include <zlib.h>

namespace pane {

namespace {

QByteArray gunzip(const char *data, qsizetype len, qsizetype expected = -1)
{
    z_stream zs{};
    if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK) throw FileError("zlib init failed");
    QByteArray out;
    out.resize(expected > 0 ? expected : std::max<qsizetype>(len * 4, 4096));
    zs.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data));
    zs.avail_in = uInt(len);
    int ret = Z_OK;
    while (ret != Z_STREAM_END) {
        if (zs.total_out >= uLong(out.size())) out.resize(out.size() * 2);
        zs.next_out = reinterpret_cast<Bytef *>(out.data() + zs.total_out);
        zs.avail_out = uInt(out.size() - qsizetype(zs.total_out));
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            inflateEnd(&zs);
            throw FileError("The .pdn file contains corrupt compressed data.");
        }
        if (ret == Z_OK && zs.avail_in == 0 && zs.avail_out != 0) break;
    }
    out.resize(qsizetype(zs.total_out));
    inflateEnd(&zs);
    return out;
}

quint32 readBE32(const QByteArray &d, qsizetype &pos)
{
    if (pos + 4 > d.size()) throw FileError("The .pdn file is truncated.");
    quint32 v = qFromBigEndian<quint32>(d.constData() + pos);
    pos += 4;
    return v;
}

BlendMode blendModeFromName(const QString &cls)
{
    static const std::pair<const char *, BlendMode> table[] = {
        {"Multiply", BlendMode::Multiply}, {"ColorBurn", BlendMode::ColorBurn}, {"ColorDodge", BlendMode::ColorDodge},
        {"Reflect", BlendMode::Reflect},   {"Glow", BlendMode::Glow},           {"Overlay", BlendMode::Overlay},
        {"Difference", BlendMode::Difference}, {"Negation", BlendMode::Negation}, {"Lighten", BlendMode::Lighten},
        {"Darken", BlendMode::Darken},     {"Screen", BlendMode::Screen},       {"Xor", BlendMode::Xor},
        {"Additive", BlendMode::Additive},
    };
    for (auto &[name, mode] : table)
        if (cls.contains(QLatin1String(name))) return mode;
    return BlendMode::Normal;
}

// Minimal NRBF (BinaryFormatter) writer for the paint.net document graph.
class NrbfWriter {
public:
    enum Bt : uint8_t { Primitive = 0, String = 1, Object = 2, SystemClass = 3, Class = 4, ObjectArray = 5 };
    enum Pt : uint8_t { Boolean = 1, Byte = 2, Int32 = 8, Int64 = 9 };
    struct Member {
        QString name;
        Bt type;
        uint8_t prim = 0;   // for Primitive
        QString className;  // for Class / SystemClass
    };
    QByteArray out;

    void u8(uint8_t v) { out.append(char(v)); }
    void i32(int32_t v) { char b[4]; qToLittleEndian(v, b); out.append(b, 4); }
    void i64(int64_t v) { char b[8]; qToLittleEndian(v, b); out.append(b, 8); }
    void str(const QString &s)
    {
        QByteArray u = s.toUtf8();
        uint32_t n = uint32_t(u.size());
        while (n >= 0x80) {
            u8(uint8_t(n | 0x80));
            n >>= 7;
        }
        u8(uint8_t(n));
        out.append(u);
    }
    void header(int root)
    {
        u8(0);
        i32(root);
        i32(-1);
        i32(1);
        i32(0);
    }
    void library(int id, const QString &name)
    {
        u8(12);
        i32(id);
        str(name);
    }
    // ClassWithMembersAndTypes (5) or SystemClassWithMembersAndTypes (4).
    void classHeader(int id, const QString &name, const std::vector<Member> &members, int libraryId)
    {
        u8(libraryId ? 5 : 4);
        i32(id);
        str(name);
        i32(int32_t(members.size()));
        for (const Member &m : members) str(m.name);
        for (const Member &m : members) u8(m.type);
        for (const Member &m : members) {
            if (m.type == Primitive) u8(m.prim);
            else if (m.type == SystemClass) str(m.className);
            else if (m.type == Class) {
                str(m.className);
                i32(libraryId);
            }
        }
        if (libraryId) i32(libraryId);
    }
    void classWithId(int id, int metadataId)
    {
        u8(1);
        i32(id);
        i32(metadataId);
    }
    void ref(int id)
    {
        u8(9);
        i32(id);
    }
    void null() { u8(10); }
    void stringObject(int id, const QString &s)
    {
        u8(6);
        i32(id);
        str(s);
    }
    void objectArray(int id, int length)
    {
        u8(16);
        i32(id);
        i32(length);
    }
    void end() { u8(11); }
};

QByteArray gzip(const char *data, qsizetype len)
{
    z_stream zs{};
    deflateInit2(&zs, 6, Z_DEFLATED, 16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    QByteArray out(qsizetype(deflateBound(&zs, uLong(len)) + 32), Qt::Uninitialized);
    zs.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data));
    zs.avail_in = uInt(len);
    zs.next_out = reinterpret_cast<Bytef *>(out.data());
    zs.avail_out = uInt(out.size());
    deflate(&zs, Z_FINISH);
    out.resize(qsizetype(zs.total_out));
    deflateEnd(&zs);
    return out;
}

const char *blendOpClass(BlendMode m)
{
    switch (m) {
    case BlendMode::Normal: return "PaintDotNet.UserBlendOps+NormalBlendOp";
    case BlendMode::Multiply: return "PaintDotNet.UserBlendOps+MultiplyBlendOp";
    case BlendMode::ColorBurn: return "PaintDotNet.UserBlendOps+ColorBurnBlendOp";
    case BlendMode::ColorDodge: return "PaintDotNet.UserBlendOps+ColorDodgeBlendOp";
    case BlendMode::Reflect: return "PaintDotNet.UserBlendOps+ReflectBlendOp";
    case BlendMode::Glow: return "PaintDotNet.UserBlendOps+GlowBlendOp";
    case BlendMode::Overlay: return "PaintDotNet.UserBlendOps+OverlayBlendOp";
    case BlendMode::Difference: return "PaintDotNet.UserBlendOps+DifferenceBlendOp";
    case BlendMode::Negation: return "PaintDotNet.UserBlendOps+NegationBlendOp";
    case BlendMode::Lighten: return "PaintDotNet.UserBlendOps+LightenBlendOp";
    case BlendMode::Darken: return "PaintDotNet.UserBlendOps+DarkenBlendOp";
    case BlendMode::Screen: return "PaintDotNet.UserBlendOps+ScreenBlendOp";
    case BlendMode::Xor: return "PaintDotNet.UserBlendOps+XorBlendOp";
    case BlendMode::Additive: return "PaintDotNet.UserBlendOps+AdditiveBlendOp";
    }
    return "PaintDotNet.UserBlendOps+NormalBlendOp";
}

// Serializes a document in paint.net's PDN3 container.
QByteArray writePdn(const Document &doc)
{
    const int n = doc.layerCount(), w = doc.width(), h = doc.height();
    const QString lib = QStringLiteral("PaintDotNet.Data, Version=3.36.0.0, Culture=neutral, PublicKeyToken=null");
    NrbfWriter nw;
    using M = NrbfWriter::Member;
    // Object ids: 1 document, 3 layer list, 4 version, 5 items array, then 10 + 10*i per layer.
    nw.header(1);
    nw.library(2, lib);
    nw.classHeader(1, QStringLiteral("PaintDotNet.Document"),
                   {M{"layers", NrbfWriter::Class, 0, "PaintDotNet.LayerList"},
                    M{"width", NrbfWriter::Primitive, NrbfWriter::Int32, {}},
                    M{"height", NrbfWriter::Primitive, NrbfWriter::Int32, {}},
                    M{"savedWith", NrbfWriter::SystemClass, 0, "System.Version"},
                    M{"userMetaData", NrbfWriter::SystemClass, 0, "System.Collections.Specialized.NameValueCollection"}},
                   2);
    nw.ref(3);
    nw.i32(w);
    nw.i32(h);
    nw.ref(4);
    nw.null();

    nw.classHeader(3, QStringLiteral("PaintDotNet.LayerList"),
                   {M{"parent", NrbfWriter::Class, 0, "PaintDotNet.Document"}, M{"ArrayList+_items", NrbfWriter::ObjectArray, 0, {}},
                    M{"ArrayList+_size", NrbfWriter::Primitive, NrbfWriter::Int32, {}},
                    M{"ArrayList+_version", NrbfWriter::Primitive, NrbfWriter::Int32, {}}},
                   2);
    nw.ref(1);
    nw.ref(5);
    nw.i32(n);
    nw.i32(n);

    nw.classHeader(4, QStringLiteral("System.Version"),
                   {M{"_Major", NrbfWriter::Primitive, NrbfWriter::Int32, {}}, M{"_Minor", NrbfWriter::Primitive, NrbfWriter::Int32, {}},
                    M{"_Build", NrbfWriter::Primitive, NrbfWriter::Int32, {}}, M{"_Revision", NrbfWriter::Primitive, NrbfWriter::Int32, {}}},
                   0);
    nw.i32(3);
    nw.i32(36);
    nw.i32(0);
    nw.i32(0);

    nw.objectArray(5, n);
    for (int i = 0; i < n; ++i) nw.ref(10 + 10 * i);

    // Layers. The first of each class carries its metadata; later ones reuse it (ClassWithId).
    for (int i = 0; i < n; ++i) {
        const Layer &l = doc.layer(i);
        const int base = 10 + 10 * i;
        const int layerId = base, bprops = base + 1, blend = base + 2, surf = base + 3, mem = base + 4, lprops = base + 5, name = base + 6;
        if (i == 0)
            nw.classHeader(layerId, QStringLiteral("PaintDotNet.BitmapLayer"),
                           {M{"properties", NrbfWriter::Class, 0, "PaintDotNet.BitmapLayer+BitmapLayerProperties"},
                            M{"surface", NrbfWriter::Class, 0, "PaintDotNet.Surface"},
                            M{"Layer+isDisposed", NrbfWriter::Primitive, NrbfWriter::Boolean, {}},
                            M{"Layer+width", NrbfWriter::Primitive, NrbfWriter::Int32, {}},
                            M{"Layer+height", NrbfWriter::Primitive, NrbfWriter::Int32, {}},
                            M{"Layer+properties", NrbfWriter::Class, 0, "PaintDotNet.Layer+LayerProperties"}},
                           2);
        else nw.classWithId(layerId, 10);
        nw.ref(bprops);
        nw.ref(surf);
        nw.u8(0);
        nw.i32(w);
        nw.i32(h);
        nw.ref(lprops);

        if (i == 0)
            nw.classHeader(bprops, QStringLiteral("PaintDotNet.BitmapLayer+BitmapLayerProperties"),
                           {M{"blendOp", NrbfWriter::Object, 0, {}}}, 2);
        else nw.classWithId(bprops, 11);
        nw.ref(blend);
        nw.classHeader(blend, QString::fromLatin1(blendOpClass(l.props.blendMode)), {}, 2);

        if (i == 0)
            nw.classHeader(surf, QStringLiteral("PaintDotNet.Surface"),
                           {M{"width", NrbfWriter::Primitive, NrbfWriter::Int32, {}}, M{"height", NrbfWriter::Primitive, NrbfWriter::Int32, {}},
                            M{"stride", NrbfWriter::Primitive, NrbfWriter::Int32, {}}, M{"scan0", NrbfWriter::Class, 0, "PaintDotNet.MemoryBlock"}},
                           2);
        else nw.classWithId(surf, 13);
        nw.i32(w);
        nw.i32(h);
        nw.i32(w * 4);
        nw.ref(mem);

        if (i == 0)
            nw.classHeader(mem, QStringLiteral("PaintDotNet.MemoryBlock"),
                           {M{"length64", NrbfWriter::Primitive, NrbfWriter::Int64, {}}, M{"hasParent", NrbfWriter::Primitive, NrbfWriter::Boolean, {}},
                            M{"deferred", NrbfWriter::Primitive, NrbfWriter::Boolean, {}}},
                           2);
        else nw.classWithId(mem, 14);
        nw.i64(int64_t(w) * h * 4);
        nw.u8(0);
        nw.u8(1);

        if (i == 0)
            nw.classHeader(lprops, QStringLiteral("PaintDotNet.Layer+LayerProperties"),
                           {M{"name", NrbfWriter::String, 0, {}},
                            M{"userMetaData", NrbfWriter::SystemClass, 0, "System.Collections.Specialized.NameValueCollection"},
                            M{"visible", NrbfWriter::Primitive, NrbfWriter::Boolean, {}},
                            M{"isBackground", NrbfWriter::Primitive, NrbfWriter::Boolean, {}},
                            M{"opacity", NrbfWriter::Primitive, NrbfWriter::Byte, {}}},
                           2);
        else nw.classWithId(lprops, 15);
        nw.stringObject(name, l.props.name);
        nw.null();
        nw.u8(l.props.visible ? 1 : 0);
        nw.u8(i == 0 ? 1 : 0);
        nw.u8(l.props.opacity);
    }
    nw.end();

    // Deferred MemoryBlock data, in record order: format 0 (gzip), chunk size,
    // then (chunk number, compressed size, data) per chunk. Chunks compress in parallel.
    const uint32_t chunkSize = 4 * 1024 * 1024;
    QByteArray deferred;
    for (int i = 0; i < n; ++i) {
        QImage img = doc.layer(i).surface.toImage();
        const qsizetype total = qsizetype(w) * h * 4;
        const int chunks = int((total + chunkSize - 1) / chunkSize);
        std::vector<QByteArray> packed(static_cast<size_t>(chunks));
        parallelFor(chunks, [&](int c) {
            qsizetype off = qsizetype(c) * chunkSize, len = std::min<qsizetype>(chunkSize, total - off);
            packed[size_t(c)] = gzip(reinterpret_cast<const char *>(img.constBits()) + off, len);
        });
        deferred.append(char(0));
        char be[4];
        qToBigEndian(chunkSize, be);
        deferred.append(be, 4);
        for (int c = 0; c < chunks; ++c) {
            qToBigEndian(uint32_t(c), be);
            deferred.append(be, 4);
            qToBigEndian(uint32_t(packed[size_t(c)].size()), be);
            deferred.append(be, 4);
            deferred.append(packed[size_t(c)]);
        }
    }

    // Header: XML with a thumbnail (shown by paint.net's file dialogs).
    QImage flat = doc.flattenToImage();
    QSize ts = flat.size().scaled(256, 256, Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
    QImage thumb = ts == flat.size() ? flat : resampleImage(flat, ts, Resampling::BestQuality);
    QByteArray xml = QStringLiteral("<pdnImage width=\"%1\" height=\"%2\" layers=\"%3\" savedWithVersion=\"3.36.0.0\"><custom><thumb png=\"%4\" /></custom></pdnImage>")
                         .arg(w)
                         .arg(h)
                         .arg(n)
                         .arg(QString::fromLatin1(encodePngParallel(thumb, true, 96, 96).toBase64()))
                         .toUtf8();
    QByteArray out("PDN3", 4);
    out.append(char(xml.size() & 0xFF));
    out.append(char((xml.size() >> 8) & 0xFF));
    out.append(char((xml.size() >> 16) & 0xFF));
    out.append(xml);
    out.append(char(0x00));
    out.append(char(0x01));
    out.append(nw.out);
    out.append(deferred);
    return out;
}

class PdnFileType final : public FileType {
public:
    QString id() const override { return QStringLiteral("pdn"); }
    QString name() const override { return QStringLiteral("Paint.NET Image"); }
    QStringList extensions() const override { return {QStringLiteral("pdn")}; }
    bool supportsLayers() const override { return true; }
    QByteArray save(const Document &doc, const QImage &, const SaveOptions &) const override { return writePdn(doc); }

    std::unique_ptr<Document> load(const QByteArray &file) const override
    {
        if (!file.startsWith("PDN3") || file.size() < 9) throw FileError("This is not a paint.net image.");
        qsizetype headerLen = uchar(file[4]) | (uchar(file[5]) << 8) | (uchar(file[6]) << 16);
        qsizetype pos = 7 + headerLen;
        if (pos + 2 > file.size()) throw FileError("The .pdn file is truncated.");

        QByteArray stream;
        bool deferred;
        if (uchar(file[pos]) == 0x00 && uchar(file[pos + 1]) == 0x01) {
            stream = file;
            pos += 2;
            deferred = true;
        } else if (uchar(file[pos]) == 0x1F && uchar(file[pos + 1]) == 0x8B) {
            stream = gunzip(file.constData() + pos, file.size() - pos);
            pos = 0;
            deferred = false;
        } else {
            throw FileError("This .pdn file was saved by a version of paint.net that Pane does not support yet.");
        }

        nrbf::Reader r;
        try {
            r.parse(stream, pos);
        } catch (const std::runtime_error &e) {
            throw FileError(e.what());
        }

        const nrbf::Object *docObj = nullptr;
        for (auto &[id, o] : r.objects())
            if (o.kind == nrbf::Object::Class && o.className == QLatin1String("PaintDotNet.Document")) docObj = &o;
        if (!docObj) throw FileError("The .pdn file does not contain a paint.net document.");

        int width = r.memberPrim(*docObj, QStringLiteral("width")).toInt();
        int height = r.memberPrim(*docObj, QStringLiteral("height")).toInt();
        if (width <= 0 || height <= 0 || width > 65535 || height > 65535) throw FileError("The .pdn file has an invalid image size.");

        // Deferred MemoryBlocks appear in record order; their data follows the stream in that order.
        std::map<int, QByteArray> blockData;
        for (int id : r.order()) {
            const nrbf::Object *o = r.object(id);
            if (!o || o->kind != nrbf::Object::Class || !o->className.endsWith(QLatin1String("MemoryBlock"))) continue;
            qint64 length = r.memberPrim(*o, QStringLiteral("length64")).toLongLong();
            if (!length) length = r.memberPrim(*o, QStringLiteral("length")).toLongLong();
            if (length <= 0 || length > qint64(65535) * 65535 * 4) throw FileError("The .pdn file has an invalid layer.");
            bool isDeferred = r.memberPrim(*o, QStringLiteral("deferred")).toBool();
            if (isDeferred) {
                if (!deferred) throw FileError("Unexpected deferred data in .pdn file.");
                blockData[id] = readDeferredBlock(stream, pos, length);
            } else {
                const nrbf::Object *arr = r.memberObject(*o, QStringLiteral("pointerData"));
                if (!arr || arr->bytes.size() < length) throw FileError("The .pdn file has missing layer data.");
                blockData[id] = arr->bytes.left(qsizetype(length));
            }
        }

        auto doc = std::make_unique<Document>(QSize(width, height));
        const nrbf::Object *layerList = r.memberObject(*docObj, QStringLiteral("layers"));
        if (!layerList) throw FileError("The .pdn file has no layers.");
        const nrbf::Object *items = r.memberObject(*layerList, QStringLiteral("_items"));
        int count = r.memberPrim(*layerList, QStringLiteral("_size")).toInt();
        if (!items || count <= 0) throw FileError("The .pdn file has no layers.");

        for (int i = 0; i < count && size_t(i) < items->values.size(); ++i) {
            const nrbf::Object *lo = r.deref(items->values[size_t(i)]);
            if (!lo) continue;
            Layer layer;
            layer.surface = Surface(width, height);

            // Layer properties (name, visibility, opacity).
            const nrbf::Object *props = nullptr;
            const nrbf::Object *bitmapProps = nullptr;
            for (size_t m = 0; m < lo->values.size(); ++m) {
                const nrbf::Object *c = r.deref(lo->values[m]);
                if (!c || c->kind != nrbf::Object::Class) continue;
                if (c->className.endsWith(QLatin1String("Layer+LayerProperties"))) props = c;
                else if (c->className.endsWith(QLatin1String("BitmapLayerProperties"))) bitmapProps = c;
            }
            if (props) {
                layer.props.name = r.memberString(*props, QStringLiteral("name"));
                QVariant vis = r.memberPrim(*props, QStringLiteral("visible"));
                layer.props.visible = vis.isValid() ? vis.toBool() : true;
                QVariant op = r.memberPrim(*props, QStringLiteral("opacity"));
                layer.props.opacity = uint8_t(op.isValid() ? std::clamp(op.toInt(), 0, 255) : 255);
            }
            if (bitmapProps) {
                if (const nrbf::Object *op = r.memberObject(*bitmapProps, QStringLiteral("blendOp")))
                    layer.props.blendMode = blendModeFromName(op->className);
                else if (QVariant bm = r.memberPrim(*bitmapProps, QStringLiteral("blendMode")); bm.isValid())
                    layer.props.blendMode = BlendMode(std::clamp(bm.toInt(), 0, BlendModeCount - 1));
            }
            if (layer.props.name.isEmpty()) layer.props.name = QStringLiteral("Layer %1").arg(i + 1);

            const nrbf::Object *surf = r.memberObject(*lo, QStringLiteral("surface"));
            if (!surf) throw FileError("A layer in the .pdn file has no pixels.");
            int stride = r.memberPrim(*surf, QStringLiteral("stride")).toInt();
            const nrbf::Value *scan0 = r.member(*surf, QStringLiteral("scan0"));
            if (!scan0 || scan0->kind != nrbf::Value::Ref || !blockData.count(scan0->ref))
                throw FileError("A layer in the .pdn file has no pixel data.");
            const QByteArray &px = blockData[scan0->ref];
            if (stride < width * 4 || px.size() < qsizetype(stride) * height) throw FileError("A layer in the .pdn file is truncated.");
            QImage img(reinterpret_cast<const uchar *>(px.constData()), width, height, stride, QImage::Format_ARGB32);
            layer.surface.writeImage(img);
            layer.surface.compact();
            doc->insertLayer(doc->layerCount(), std::move(layer));
        }
        if (doc->layerCount() == 0) throw FileError("The .pdn file has no layers.");
        doc->setActiveLayerIndex(doc->layerCount() - 1);
        doc->history().reset(std::make_unique<BaseHistoryItem>(QStringLiteral("Open Image"), glyphs::Image));
        return doc;
    }

private:
    static QByteArray readDeferredBlock(const QByteArray &d, qsizetype &pos, qint64 length)
    {
        if (pos >= d.size()) throw FileError("The .pdn file is truncated.");
        uint8_t format = uint8_t(d[pos++]); // 0 = gzip chunks, 1 = raw chunks
        if (format > 1) throw FileError("Unsupported .pdn layer data format.");
        quint32 chunkSize = readBE32(d, pos);
        if (chunkSize == 0) throw FileError("Corrupt .pdn layer data.");
        qint64 chunkCount = (length + chunkSize - 1) / chunkSize;
        QByteArray out(qsizetype(length), Qt::Uninitialized);
        for (qint64 i = 0; i < chunkCount; ++i) {
            quint32 number = readBE32(d, pos);
            quint32 size = readBE32(d, pos);
            if (number >= chunkCount || pos + qsizetype(size) > d.size()) throw FileError("Corrupt .pdn layer data.");
            qint64 offset = qint64(number) * chunkSize;
            qint64 expect = std::min<qint64>(chunkSize, length - offset);
            QByteArray chunk = format == 0 ? gunzip(d.constData() + pos, size, qsizetype(expect)) : d.mid(pos, size);
            if (chunk.size() != expect) throw FileError("Corrupt .pdn layer data.");
            std::memcpy(out.data() + offset, chunk.constData(), size_t(expect));
            pos += size;
        }
        return out;
    }
};

} // namespace

const FileType *pdnFileType() { static PdnFileType t; return &t; }

} // namespace pane
