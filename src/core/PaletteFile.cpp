#include "core/PaletteFile.h"

#include <QStringList>

namespace pane {

std::vector<Pixel> parsePalette(const QString &text)
{
    std::vector<Pixel> out;
    for (QString line : text.split(QLatin1Char('\n'))) {
        int c = int(line.indexOf(QLatin1Char(';')));
        if (c >= 0) line.truncate(c);
        line = line.trimmed();
        if (line.isEmpty()) continue;
        bool ok = false;
        uint v = line.toUInt(&ok, 16);
        if (!ok) continue;
        if (line.size() == 6) v |= 0xFF000000u; // RRGGBB without alpha
        out.push_back(Pixel(v));
        if (int(out.size()) == PaletteSize) break;
    }
    while (int(out.size()) < PaletteSize) out.push_back(0xFFFFFFFFu);
    return out;
}

QString formatPalette(const std::vector<Pixel> &colors)
{
    QString s = QStringLiteral("; paint.net Palette File\n; Lines that start with a semicolon are comments\n"
                               "; Colors are written as 8-digit hexadecimal numbers: aarrggbb\n");
    for (int i = 0; i < PaletteSize; ++i) {
        Pixel p = i < int(colors.size()) ? colors[size_t(i)] : 0xFFFFFFFFu;
        s += QStringLiteral("%1\n").arg(p, 8, 16, QLatin1Char('0')).toUpper();
    }
    return s;
}

} // namespace pane
