#include "io/Zip.h"

#include "core/Parallel.h"

#include <QtEndian>
#include <stdexcept>
#include <zlib.h>

namespace pane::zip {

namespace {

void p16(QByteArray &o, uint16_t v) { char b[2]; qToLittleEndian(v, b); o.append(b, 2); }
void p32(QByteArray &o, uint32_t v) { char b[4]; qToLittleEndian(v, b); o.append(b, 4); }
uint16_t g16(const QByteArray &d, qsizetype at)
{
    if (at + 2 > d.size()) throw std::runtime_error("truncated zip");
    return qFromLittleEndian<quint16>(d.constData() + at);
}
uint32_t g32(const QByteArray &d, qsizetype at)
{
    if (at + 4 > d.size()) throw std::runtime_error("truncated zip");
    return qFromLittleEndian<quint32>(d.constData() + at);
}

QByteArray deflateRaw(const QByteArray &in)
{
    z_stream zs{};
    deflateInit2(&zs, 6, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
    QByteArray out(qsizetype(deflateBound(&zs, uLong(in.size()))), Qt::Uninitialized);
    zs.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(in.constData()));
    zs.avail_in = uInt(in.size());
    zs.next_out = reinterpret_cast<Bytef *>(out.data());
    zs.avail_out = uInt(out.size());
    deflate(&zs, Z_FINISH);
    out.resize(qsizetype(zs.total_out));
    deflateEnd(&zs);
    return out;
}

QByteArray inflateRaw(const char *data, qsizetype len, qsizetype expected)
{
    z_stream zs{};
    if (inflateInit2(&zs, -15) != Z_OK) throw std::runtime_error("zlib");
    QByteArray out(expected, Qt::Uninitialized);
    zs.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data));
    zs.avail_in = uInt(len);
    zs.next_out = reinterpret_cast<Bytef *>(out.data());
    zs.avail_out = uInt(out.size());
    int r = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    if (r != Z_STREAM_END || qsizetype(zs.total_out) != expected) throw std::runtime_error("corrupt zip entry");
    return out;
}

} // namespace

QByteArray write(const std::vector<Entry> &entries)
{
    // Compress entries in parallel (layer PNGs are already compressed; deflate is cheap on them).
    std::vector<QByteArray> packed(entries.size());
    std::vector<uint32_t> crcs(entries.size());
    parallelFor(int(entries.size()), [&](int i) {
        const Entry &e = entries[size_t(i)];
        crcs[size_t(i)] = uint32_t(crc32(0, reinterpret_cast<const Bytef *>(e.data.constData()), uInt(e.data.size())));
        packed[size_t(i)] = e.compress ? deflateRaw(e.data) : e.data;
    });
    QByteArray out, central;
    for (size_t i = 0; i < entries.size(); ++i) {
        const Entry &e = entries[i];
        QByteArray name = e.name.toUtf8();
        const bool deflated = e.compress && packed[i].size() < e.data.size();
        const QByteArray &body = deflated ? packed[i] : e.data;
        const uint32_t offset = uint32_t(out.size());
        p32(out, 0x04034b50);
        p16(out, 20);
        p16(out, 0x0800); // UTF-8 names
        p16(out, deflated ? 8 : 0);
        p16(out, 0);
        p16(out, 0x21); // 1980-01-01
        p32(out, crcs[i]);
        p32(out, uint32_t(body.size()));
        p32(out, uint32_t(e.data.size()));
        p16(out, uint16_t(name.size()));
        p16(out, 0);
        out.append(name);
        out.append(body);

        p32(central, 0x02014b50);
        p16(central, 20);
        p16(central, 20);
        p16(central, 0x0800);
        p16(central, deflated ? 8 : 0);
        p16(central, 0);
        p16(central, 0x21);
        p32(central, crcs[i]);
        p32(central, uint32_t(body.size()));
        p32(central, uint32_t(e.data.size()));
        p16(central, uint16_t(name.size()));
        p16(central, 0);
        p16(central, 0);
        p16(central, 0);
        p16(central, 0);
        p32(central, 0);
        p32(central, offset);
        central.append(name);
    }
    const uint32_t cdOffset = uint32_t(out.size());
    out.append(central);
    p32(out, 0x06054b50);
    p16(out, 0);
    p16(out, 0);
    p16(out, uint16_t(entries.size()));
    p16(out, uint16_t(entries.size()));
    p32(out, uint32_t(central.size()));
    p32(out, cdOffset);
    p16(out, 0);
    return out;
}

std::map<QString, QByteArray> read(const QByteArray &d)
{
    // Find the end-of-central-directory record.
    qsizetype eocd = -1;
    for (qsizetype i = d.size() - 22; i >= std::max<qsizetype>(0, d.size() - 65557); --i)
        if (g32(d, i) == 0x06054b50) {
            eocd = i;
            break;
        }
    if (eocd < 0) throw std::runtime_error("not a zip archive");
    const int count = g16(d, eocd + 10);
    qsizetype cd = g32(d, eocd + 16);
    std::map<QString, QByteArray> files;
    for (int i = 0; i < count; ++i) {
        if (g32(d, cd) != 0x02014b50) throw std::runtime_error("bad central directory");
        const int method = g16(d, cd + 10);
        const uint32_t csize = g32(d, cd + 20), usize = g32(d, cd + 24);
        const int nameLen = g16(d, cd + 28), extraLen = g16(d, cd + 30), commentLen = g16(d, cd + 32);
        const uint32_t local = g32(d, cd + 42);
        QString name = QString::fromUtf8(d.constData() + cd + 46, nameLen);
        cd += 46 + nameLen + extraLen + commentLen;
        if (g32(d, local) != 0x04034b50) throw std::runtime_error("bad local header");
        qsizetype dataAt = local + 30 + g16(d, local + 26) + g16(d, local + 28);
        if (dataAt + qsizetype(csize) > d.size()) throw std::runtime_error("truncated zip entry");
        if (method == 0) files[name] = d.mid(dataAt, csize);
        else if (method == 8) files[name] = inflateRaw(d.constData() + dataAt, csize, usize);
        else throw std::runtime_error("unsupported zip compression");
    }
    return files;
}

} // namespace pane::zip
