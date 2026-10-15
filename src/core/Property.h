// SPDX-License-Identifier: LGPL-3.0-only
// Pane — a paint.net-style image editor for Linux. Written by Claude (Anthropic).

#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <cstdint>

namespace pane {

enum class PropType { Int, Double, Bool, Choice, Seed, Color };

// One user-editable effect parameter (drives the auto-generated dialog).
struct PropertySpec {
    QString id, label;
    PropType type = PropType::Int;
    double min = 0, max = 100, def = 0;
    int decimals = 0;
    QStringList choices;
    QString suffix;

    static PropertySpec integer(QString id, QString label, int min, int max, int def, QString suffix = {})
    {
        return {std::move(id), std::move(label), PropType::Int, double(min), double(max), double(def), 0, {}, std::move(suffix)};
    }
    static PropertySpec real(QString id, QString label, double min, double max, double def, int decimals)
    {
        return {std::move(id), std::move(label), PropType::Double, min, max, def, decimals, {}, {}};
    }
    static PropertySpec boolean(QString id, QString label, bool def)
    {
        return {std::move(id), std::move(label), PropType::Bool, 0, 1, def ? 1.0 : 0.0, 0, {}, {}};
    }
    static PropertySpec choice(QString id, QString label, QStringList choices, int def)
    {
        return {std::move(id), std::move(label), PropType::Choice, 0, double(choices.size() - 1), double(def), 0, std::move(choices), {}};
    }
    static PropertySpec seed(QString id = QStringLiteral("seed"))
    {
        return {std::move(id), QStringLiteral("Random seed"), PropType::Seed, 0, 2147483647.0, 0, 0, {}, {}};
    }
    // def is a 0xAARRGGBB color.
    static PropertySpec color(QString id, QString label, uint32_t def)
    {
        return {std::move(id), std::move(label), PropType::Color, 0, 0, double(def), 0, {}, {}};
    }
};

// Default values for a property list.
QVariantMap propertyDefaults(const QList<PropertySpec> &props);

} // namespace pane
