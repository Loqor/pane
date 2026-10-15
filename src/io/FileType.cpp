// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#include "io/FileType.h"

#include "core/Glyphs.h"
#include "io/Formats.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace pane {

std::unique_ptr<Document> documentFromImage(const QImage &img, double dpiX, double dpiY)
{
    auto doc = std::make_unique<Document>(img.size());
    Layer bg;
    bg.props.name = QStringLiteral("Background");
    bg.surface = Surface::fromImage(img.format() == QImage::Format_ARGB32 ? img : img.convertToFormat(QImage::Format_ARGB32));
    doc->insertLayer(0, std::move(bg));
    doc->setDpi(dpiX, dpiY);
    doc->history().reset(std::make_unique<BaseHistoryItem>(QStringLiteral("Open Image"), glyphs::Image));
    return doc;
}

namespace filetypes {

const std::vector<const FileType *> &all()
{
    static const std::vector<const FileType *> types = [] {
        std::vector<const FileType *> v;
        v.push_back(pdnFileType());
        v.push_back(pngFileType());
        v.push_back(jpegFileType());
        v.push_back(bmpFileType());
        v.push_back(webpFileType());
        v.push_back(avifFileType());
        v.push_back(heicFileType());
        v.push_back(tiffFileType());
        v.push_back(gifFileType());
        v.push_back(tgaFileType());
        v.push_back(ddsFileType());
        v.push_back(icoFileType());
        v.push_back(oraFileType());
        return v;
    }();
    return types;
}

const FileType *byId(const QString &id)
{
    for (auto *t : all())
        if (t->id() == id) return t;
    return nullptr;
}

const FileType *forPath(const QString &path)
{
    QString ext = QFileInfo(path).suffix().toLower();
    for (auto *t : all())
        if (t->extensions().contains(ext)) return t;
    return nullptr;
}

static QString filterFor(const FileType *t)
{
    QStringList globs;
    for (auto &e : t->extensions()) globs << QStringLiteral("*.") + e;
    return QStringLiteral("%1 (%2)").arg(t->name(), globs.join(' '));
}

QString openFilter()
{
    QStringList globs, filters;
    for (auto *t : all()) {
        if (!t->canLoad()) continue;
        for (auto &e : t->extensions()) globs << QStringLiteral("*.") + e;
        filters << filterFor(t);
    }
    return QStringLiteral("All images (%1);;%2;;All files (*)").arg(globs.join(' '), filters.join(QStringLiteral(";;")));
}

QString saveFilter(const FileType *first)
{
    QStringList filters;
    if (first && first->canSave()) filters << filterFor(first);
    for (auto *t : all())
        if (t->canSave() && t != first) filters << filterFor(t);
    return filters.join(QStringLiteral(";;"));
}

const FileType *fromFilter(const QString &filter)
{
    for (auto *t : all())
        if (filterFor(t) == filter) return t;
    return nullptr;
}

static const FileType *sniff(const QByteArray &d)
{
    if (d.startsWith("PDN3")) return pdnFileType();
    if (d.startsWith("\x89PNG")) return pngFileType();
    if (d.startsWith("\xFF\xD8\xFF")) return jpegFileType();
    if (d.startsWith("BM")) return bmpFileType();
    if (d.endsWith(QByteArray("TRUEVISION-XFILE.\0", 18))) return tgaFileType();
    if (d.startsWith("RIFF") && d.mid(8, 4) == "WEBP") return webpFileType();
    if (d.mid(4, 4) == "ftyp") {
        QByteArray brand = d.mid(8, 4);
        if (brand == "avif" || brand == "avis") return avifFileType();
        return heicFileType(); // heic, heix, mif1, msf1, ...
    }
    if (d.startsWith("II*") || d.startsWith("MM\x00*")) return tiffFileType();
    if (d.startsWith("GIF8")) return gifFileType();
    if (d.startsWith("DDS ")) return ddsFileType();
    if (d.startsWith(QByteArray("\x00\x00\x01\x00", 4))) return icoFileType();
    if (d.startsWith("PK\x03\x04") && d.mid(30, 8) == "mimetype" && d.mid(38, 16) == "image/openraster") return oraFileType();
    return nullptr;
}

std::unique_ptr<Document> load(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) throw FileError(QStringLiteral("Could not open the file: %1").arg(f.errorString()).toStdString());
    QByteArray data = f.readAll();
    if (f.error() != QFile::NoError) throw FileError(f.errorString().toStdString());
    const FileType *t = sniff(data);
    if (!t) t = forPath(path);
    if (!t || !t->canLoad()) throw FileError("This file type is not supported.");
    std::unique_ptr<Document> doc;
    try {
        doc = t->load(data);
    } catch (const std::bad_alloc &) {
        throw FileError("There is not enough memory to open this image.");
    }
    doc->setFilePath(QFileInfo(path).absoluteFilePath());
    doc->setFileTypeId(t->id());
    doc->markClean();
    return doc;
}

void writeFileAtomically(const QString &path, const QByteArray &data)
{
    QFileInfo fi(path);
    QString tmp = fi.absolutePath() + QStringLiteral("/.") + fi.fileName() + QStringLiteral(".pane-tmp");
    QByteArray tmpPath = QFile::encodeName(tmp), finalPath = QFile::encodeName(fi.absoluteFilePath());
    int fd = ::open(tmpPath.constData(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) throw FileError(QStringLiteral("Could not write to %1: %2").arg(fi.absolutePath(), QString::fromLocal8Bit(strerror(errno))).toStdString());
    const char *p = data.constData();
    qint64 left = data.size();
    while (left > 0) {
        ssize_t n = ::write(fd, p, size_t(left));
        if (n < 0) {
            if (errno == EINTR) continue;
            int err = errno;
            ::close(fd);
            ::unlink(tmpPath.constData());
            throw FileError(QStringLiteral("Could not write the file: %1").arg(QString::fromLocal8Bit(strerror(err))).toStdString());
        }
        p += n;
        left -= n;
    }
    // Keep the original file's permissions when overwriting.
    if (fi.exists()) {
        struct stat st;
        if (::stat(finalPath.constData(), &st) == 0) ::fchmod(fd, st.st_mode & 07777);
    }
    if (::fsync(fd) != 0 || ::close(fd) != 0) {
        ::unlink(tmpPath.constData());
        throw FileError("Could not flush the file to disk.");
    }
    if (::rename(tmpPath.constData(), finalPath.constData()) != 0) {
        int err = errno;
        ::unlink(tmpPath.constData());
        throw FileError(QStringLiteral("Could not replace the file: %1").arg(QString::fromLocal8Bit(strerror(err))).toStdString());
    }
    int dfd = ::open(QFile::encodeName(fi.absolutePath()).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd >= 0) {
        ::fsync(dfd);
        ::close(dfd);
    }
}

void save(Document &doc, const QString &path, const FileType *type, const SaveOptions &opts)
{
    if (!type || !type->canSave()) throw FileError("Saving in this format is not supported yet.");
    QByteArray data;
    try {
        data = type->save(doc, type->supportsLayers() ? QImage() : doc.flattenToImage(), opts);
    } catch (const std::bad_alloc &) {
        throw FileError("There is not enough memory to save this image.");
    }
    writeFileAtomically(path, data);
    doc.setFilePath(QFileInfo(path).absoluteFilePath());
    doc.setFileTypeId(type->id());
    doc.markClean();
}

} // namespace filetypes
} // namespace pane
