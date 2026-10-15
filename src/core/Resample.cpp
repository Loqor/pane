// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#include "core/Resample.h"

#include "core/Color.h"

#include "core/Parallel.h"

#include <cmath>
#include <functional>
#include <stdexcept>
#include <vector>

namespace pane {

QString resamplingName(Resampling r)
{
    switch (r) {
    case Resampling::BestQuality: return QStringLiteral("Best Quality");
    case Resampling::Bicubic: return QStringLiteral("Bicubic");
    case Resampling::Bilinear: return QStringLiteral("Bilinear");
    case Resampling::NearestNeighbor: return QStringLiteral("Nearest Neighbor");
    }
    return {};
}

namespace {

struct Kernel {
    double support;
    std::function<double(double)> f;
};

Kernel boxKernel() { return {0.5, [](double x) { return std::abs(x) <= 0.5 ? 1.0 : 0.0; }}; }
Kernel triangleKernel() { return {1.0, [](double x) { x = std::abs(x); return x < 1 ? 1 - x : 0.0; }}; }
Kernel cubicKernel()
{
    // Catmull-Rom (B=0, C=0.5).
    return {2.0, [](double x) {
                x = std::abs(x);
                if (x < 1) return 1.5 * x * x * x - 2.5 * x * x + 1;
                if (x < 2) return -0.5 * x * x * x + 2.5 * x * x - 4 * x + 2;
                return 0.0;
            }};
}

struct Contrib {
    int first;
    std::vector<float> w;
};

std::vector<Contrib> contributions(int srcLen, int dstLen, const Kernel &k)
{
    std::vector<Contrib> out(static_cast<size_t>(dstLen));
    double scale = double(dstLen) / srcLen;
    double fscale = scale < 1 ? 1 / scale : 1; // stretch kernel when shrinking
    double support = k.support * fscale;
    for (int i = 0; i < dstLen; ++i) {
        double center = (i + 0.5) / scale;
        int left = int(std::floor(center - support));
        int right = int(std::ceil(center + support));
        Contrib c;
        c.first = std::max(0, left);
        double sum = 0;
        for (int j = c.first; j <= std::min(srcLen - 1, right); ++j) {
            double w = k.f((j + 0.5 - center) / fscale);
            c.w.push_back(float(w));
            sum += w;
        }
        if (sum == 0) { // degenerate: nearest
            c.first = std::clamp(int(center), 0, srcLen - 1);
            c.w = {1.0f};
        } else {
            for (auto &w : c.w) w = float(w / sum);
        }
        out[size_t(i)] = std::move(c);
    }
    return out;
}

} // namespace

QImage resampleImage(const QImage &srcIn, QSize size, Resampling mode)
{
    if (size.isEmpty()) throw std::invalid_argument("Invalid size");
    if (size == srcIn.size()) return srcIn.convertToFormat(QImage::Format_ARGB32);
    if (mode == Resampling::NearestNeighbor)
        return srcIn.convertToFormat(QImage::Format_ARGB32).scaled(size, Qt::IgnoreAspectRatio, Qt::FastTransformation);

    Kernel kx, ky;
    auto pick = [&](bool shrink) {
        switch (mode) {
        case Resampling::BestQuality: return shrink ? boxKernel() : cubicKernel();
        case Resampling::Bicubic: return cubicKernel();
        default: return triangleKernel();
        }
    };
    kx = pick(size.width() < srcIn.width());
    ky = pick(size.height() < srcIn.height());

    // Streamed two-pass filter: rows are premultiplied into float on the fly
    // (no full-image float copies), and output is written straight to ARGB32.
    const QImage src = srcIn.format() == QImage::Format_ARGB32 ? srcIn : srcIn.convertToFormat(QImage::Format_ARGB32);
    if (src.isNull()) throw std::bad_alloc();
    const int sw = src.width(), sh = src.height(), dw = size.width(), dh = size.height();

    // Horizontal pass: sw x sh -> dw x sh (premultiplied float).
    std::vector<float> tmp(size_t(dw) * sh * 4);
    auto cx = contributions(sw, dw, kx);
    parallelFor(sh, [&](int y) {
        thread_local std::vector<float> row;
        row.resize(size_t(sw) * 4);
        auto *in = reinterpret_cast<const Pixel *>(src.constScanLine(y));
        for (int x = 0; x < sw; ++x) {
            Pixel p = in[x];
            float a = pxA(p) / 255.0f;
            row[size_t(x) * 4 + 0] = pxR(p) * a;
            row[size_t(x) * 4 + 1] = pxG(p) * a;
            row[size_t(x) * 4 + 2] = pxB(p) * a;
            row[size_t(x) * 4 + 3] = float(pxA(p));
        }
        float *out = &tmp[size_t(y) * dw * 4];
        for (int x = 0; x < dw; ++x) {
            const Contrib &c = cx[size_t(x)];
            float acc[4] = {0, 0, 0, 0};
            for (size_t k = 0; k < c.w.size(); ++k) {
                const float *p = &row[size_t(c.first + int(k)) * 4];
                for (int ch = 0; ch < 4; ++ch) acc[ch] += p[ch] * c.w[k];
            }
            for (int ch = 0; ch < 4; ++ch) out[x * 4 + ch] = acc[ch];
        }
    });

    // Vertical pass: dw x sh -> dw x dh, unpremultiplied into ARGB32.
    QImage dst(dw, dh, QImage::Format_ARGB32);
    if (dst.isNull()) throw std::bad_alloc();
    auto cy = contributions(sh, dh, ky);
    parallelFor(dh, [&](int y) {
        thread_local std::vector<float> acc;
        acc.assign(size_t(dw) * 4, 0.0f);
        const Contrib &c = cy[size_t(y)];
        for (size_t k = 0; k < c.w.size(); ++k) {
            const float *in = &tmp[size_t(c.first + int(k)) * dw * 4];
            float w = c.w[k];
            for (int i = 0; i < dw * 4; ++i) acc[size_t(i)] += in[i] * w;
        }
        auto *out = reinterpret_cast<Pixel *>(dst.scanLine(y));
        for (int x = 0; x < dw; ++x) {
            const float *p = &acc[size_t(x) * 4];
            // Cubic can overshoot: clamp alpha, and color to alpha (premultiplied).
            float a = std::clamp(p[3], 0.0f, 255.0f);
            if (a < 0.5f) {
                out[x] = 0;
                continue;
            }
            float k = 255.0f / a;
            auto ch = [&](float v) { return uint32_t(std::clamp(std::lround(std::clamp(v, 0.0f, a) * k), 0L, 255L)); };
            out[x] = makePixel(ch(p[0]), ch(p[1]), ch(p[2]), uint32_t(std::lround(a)));
        }
    });
    return dst;
}

} // namespace pane
