#pragma once

#include "core/Color.h"

#include <QPointF>
#include <QStringList>
#include <cmath>
#include <numbers>

namespace pane {

enum class GradientType { Linear, LinearReflected, LinearDiamond, Radial, Conical, SpiralCW, SpiralCCW };
enum class GradientRepeat { None, Sawtooth, Triangular };

inline QStringList gradientTypeNames()
{
    return {QStringLiteral("Linear"), QStringLiteral("Linear (Reflected)"), QStringLiteral("Linear (Diamond)"),
            QStringLiteral("Radial"), QStringLiteral("Conical"), QStringLiteral("Spiral (Clockwise)"),
            QStringLiteral("Spiral (Counter-Clockwise)")};
}

// Gradient parameter t in [0, 1] for point p, gradient from a to b.
inline double gradientT(GradientType type, GradientRepeat repeat, QPointF a, QPointF b, QPointF p)
{
    const double dx = b.x() - a.x(), dy = b.y() - a.y();
    const double len2 = dx * dx + dy * dy;
    if (len2 < 1e-9) return 0.0;
    const double len = std::sqrt(len2);
    const double px = p.x() - a.x(), py = p.y() - a.y();
    const double along = (px * dx + py * dy) / len2;
    const double across = (px * dy - py * dx) / len2;
    double t = 0;
    switch (type) {
    case GradientType::Linear: t = along; break;
    case GradientType::LinearReflected: t = std::abs(along); break;
    case GradientType::LinearDiamond: t = std::abs(along) + std::abs(across); break;
    case GradientType::Radial: t = std::sqrt(px * px + py * py) / len; break;
    case GradientType::Conical: {
        double ang = std::atan2(py, px) - std::atan2(dy, dx);
        ang = std::remainder(ang, 2 * std::numbers::pi);
        t = std::abs(ang) / std::numbers::pi;
        return t;
    }
    case GradientType::SpiralCW:
    case GradientType::SpiralCCW: {
        double ang = std::atan2(py, px) - std::atan2(dy, dx);
        double turn = ang / (2 * std::numbers::pi);
        if (type == GradientType::SpiralCCW) turn = -turn;
        double r = std::sqrt(px * px + py * py) / len;
        t = turn + r;
        return t - std::floor(t);
    }
    }
    switch (repeat) {
    case GradientRepeat::None: return std::clamp(t, 0.0, 1.0);
    case GradientRepeat::Sawtooth: return t - std::floor(t);
    case GradientRepeat::Triangular: {
        double m = t - 2.0 * std::floor(t / 2.0);
        return m > 1.0 ? 2.0 - m : m;
    }
    }
    return t;
}

// Interpolates two straight-alpha colors in premultiplied space.
inline Pixel lerpColorPremul(Pixel a, Pixel b, double t)
{
    Pixel pa = premultiply(a), pb = premultiply(b);
    auto l = [t](uint32_t x, uint32_t y) { return uint32_t(std::lround(x + (double(y) - x) * t)); };
    return unpremultiply(makePixel(l(pxR(pa), pxR(pb)), l(pxG(pa), pxG(pb)), l(pxB(pa), pxB(pb)), l(pxA(pa), pxA(pb))));
}

} // namespace pane
