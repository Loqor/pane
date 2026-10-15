#include "core/Property.h"

namespace pane {

QVariantMap propertyDefaults(const QList<PropertySpec> &props)
{
    QVariantMap m;
    for (const PropertySpec &p : props) {
        switch (p.type) {
        case PropType::Int:
        case PropType::Choice:
        case PropType::Seed: m[p.id] = int(p.def); break;
        case PropType::Color: m[p.id] = uint(p.def); break;
        case PropType::Double: m[p.id] = p.def; break;
        case PropType::Bool: m[p.id] = p.def != 0; break;
        }
    }
    return m;
}

} // namespace pane
