#include "io/Nrbf.h"

#include <QtEndian>
#include <cstring>
#include <stdexcept>

namespace pane::nrbf {

namespace {
enum RecordType : uint8_t {
    SerializedStreamHeader = 0,
    ClassWithId = 1,
    SystemClassWithMembers = 2,
    ClassWithMembers = 3,
    SystemClassWithMembersAndTypes = 4,
    ClassWithMembersAndTypes = 5,
    BinaryObjectString = 6,
    BinaryArray = 7,
    MemberPrimitiveTyped = 8,
    MemberReference = 9,
    ObjectNull = 10,
    MessageEnd = 11,
    BinaryLibrary = 12,
    ObjectNullMultiple256 = 13,
    ObjectNullMultiple = 14,
    ArraySinglePrimitive = 15,
    ArraySingleObject = 16,
    ArraySingleString = 17,
};
enum BinaryType : uint8_t { BtPrimitive = 0, BtString, BtObject, BtSystemClass, BtClass, BtObjectArray, BtStringArray, BtPrimitiveArray };
enum PrimType { PBoolean = 1, PByte = 2, PChar = 3, PDecimal = 5, PDouble = 6, PInt16 = 7, PInt32 = 8, PInt64 = 9,
                PSByte = 10, PSingle = 11, PTimeSpan = 12, PDateTime = 13, PUInt16 = 14, PUInt32 = 15, PUInt64 = 16,
                PNull = 17, PString = 18 };

[[noreturn]] void fail(const char *what) { throw std::runtime_error(std::string("Malformed .NET serialization data: ") + what); }
} // namespace

void Reader::need(qsizetype n)
{
    if (n < 0 || m_pos + n > m_data->size()) fail("unexpected end of data");
}

uint8_t Reader::readByte()
{
    need(1);
    return uint8_t((*m_data)[m_pos++]);
}

int Reader::readInt32()
{
    need(4);
    int v = qFromLittleEndian<qint32>(m_data->constData() + m_pos);
    m_pos += 4;
    return v;
}

QString Reader::readString()
{
    // 7-bit encoded length prefix.
    qint64 len = 0;
    for (int shift = 0;; shift += 7) {
        if (shift > 28) fail("bad string length");
        uint8_t b = readByte();
        len |= qint64(b & 0x7F) << shift;
        if (!(b & 0x80)) break;
    }
    need(len);
    QString s = QString::fromUtf8(m_data->constData() + m_pos, len);
    m_pos += len;
    return s;
}

QVariant Reader::readPrimitive(int type)
{
    auto rd = [&](auto tag) {
        using T = decltype(tag);
        need(sizeof(T));
        T v = qFromLittleEndian<T>(m_data->constData() + m_pos);
        m_pos += sizeof(T);
        return v;
    };
    switch (type) {
    case PBoolean: return bool(readByte());
    case PByte: return uint(readByte());
    case PSByte: return int(int8_t(readByte()));
    case PChar: {
        uint8_t b = readByte();
        int extra = b >= 0xF0 ? 3 : b >= 0xE0 ? 2 : b >= 0xC0 ? 1 : 0;
        QByteArray utf8(1, char(b));
        for (int i = 0; i < extra; ++i) utf8.append(char(readByte()));
        return QString::fromUtf8(utf8);
    }
    case PDecimal: return readString();
    case PDouble: { quint64 bits = rd(quint64{}); double d; std::memcpy(&d, &bits, 8); return d; }
    case PSingle: { quint32 bits = rd(quint32{}); float f; std::memcpy(&f, &bits, 4); return double(f); }
    case PInt16: return int(rd(qint16{}));
    case PUInt16: return uint(rd(quint16{}));
    case PInt32: return int(rd(qint32{}));
    case PUInt32: return uint(rd(quint32{}));
    case PInt64:
    case PTimeSpan:
    case PDateTime: return qlonglong(rd(qint64{}));
    case PUInt64: return qulonglong(rd(quint64{}));
    case PNull: return {};
    case PString: return readString();
    }
    fail("unknown primitive type");
}

void Reader::readClassInfo(ClassMeta &m, int &id)
{
    id = readInt32();
    m.name = readString();
    int count = readInt32();
    if (count < 0 || count > 100000) fail("bad member count");
    for (int i = 0; i < count; ++i) m.memberNames << readString();
}

void Reader::readMemberTypeInfo(ClassMeta &m)
{
    int n = int(m.memberNames.size());
    m.binaryTypes.resize(size_t(n));
    m.primTypes.assign(size_t(n), 0);
    for (int i = 0; i < n; ++i) m.binaryTypes[size_t(i)] = readByte();
    for (int i = 0; i < n; ++i) {
        switch (m.binaryTypes[size_t(i)]) {
        case BtPrimitive:
        case BtPrimitiveArray: m.primTypes[size_t(i)] = readByte(); break;
        case BtSystemClass: readString(); break;
        case BtClass: readString(); readInt32(); break;
        default: break;
        }
    }
}

void Reader::addObject(Object o)
{
    int id = o.id;
    m_order.push_back(id);
    m_objects[id] = std::move(o);
}

Value Reader::readClass(int id, const ClassMeta &meta)
{
    Object o;
    o.kind = Object::Class;
    o.id = id;
    o.className = meta.name;
    o.memberNames = meta.memberNames;
    // Register before reading members so the order reflects record order.
    m_order.push_back(id);
    m_objects[id] = o;
    std::vector<Value> values;
    for (int i = 0; i < meta.memberNames.size(); ++i) {
        if (meta.typed && meta.binaryTypes[size_t(i)] == BtPrimitive) {
            Value v;
            v.kind = Value::Primitive;
            v.prim = readPrimitive(meta.primTypes[size_t(i)]);
            values.push_back(v);
        } else {
            values.push_back(readRecord());
        }
    }
    m_objects[id].values = std::move(values);
    Value v;
    v.kind = Value::Ref;
    v.ref = id;
    return v;
}

void Reader::readArrayElements(Object &arr, int count, uint8_t binaryType, int primType)
{
    if (count < 0) fail("bad array length");
    if (binaryType == BtPrimitive) {
        arr.kind = Object::PrimitiveArray;
        if (primType == PByte) {
            need(count);
            arr.bytes = m_data->mid(m_pos, count);
            m_pos += count;
        } else {
            for (int i = 0; i < count; ++i) arr.prims << readPrimitive(primType);
        }
        return;
    }
    arr.kind = Object::ObjectArray;
    int filled = 0;
    while (filled < count) {
        Value v = readRecord();
        filled += v.kind == Value::Null ? v.nullCount : 1;
        if (v.kind == Value::Null) {
            for (int i = 0; i < v.nullCount; ++i) arr.values.push_back(Value{});
        } else {
            arr.values.push_back(v);
        }
    }
}

Value Reader::readRecord(bool *messageEnd)
{
    uint8_t type = readByte();
    switch (type) {
    case SerializedStreamHeader: {
        m_root = readInt32();
        readInt32();
        readInt32();
        readInt32();
        return readRecord(messageEnd);
    }
    case BinaryLibrary:
        readInt32();
        readString();
        return readRecord(messageEnd);
    case ClassWithId: {
        int id = readInt32();
        int metaId = readInt32();
        auto it = m_metas.find(metaId);
        if (it == m_metas.end()) fail("unknown metadata id");
        ClassMeta meta = it->second;
        return readClass(id, meta);
    }
    case SystemClassWithMembers:
    case ClassWithMembers: {
        ClassMeta meta;
        int id;
        readClassInfo(meta, id);
        if (type == ClassWithMembers) readInt32();
        meta.typed = false;
        m_metas[id] = meta;
        return readClass(id, meta);
    }
    case SystemClassWithMembersAndTypes:
    case ClassWithMembersAndTypes: {
        ClassMeta meta;
        int id;
        readClassInfo(meta, id);
        readMemberTypeInfo(meta);
        if (type == ClassWithMembersAndTypes) readInt32();
        m_metas[id] = meta;
        return readClass(id, meta);
    }
    case BinaryObjectString: {
        Object o;
        o.kind = Object::String;
        o.id = readInt32();
        o.str = readString();
        Value v;
        v.kind = Value::Ref;
        v.ref = o.id;
        addObject(std::move(o));
        return v;
    }
    case BinaryArray: {
        Object o;
        o.id = readInt32();
        uint8_t arrayType = readByte();
        int rank = readInt32();
        if (rank < 1 || rank > 32) fail("bad array rank");
        qint64 total = 1;
        for (int i = 0; i < rank; ++i) total *= readInt32();
        if (arrayType >= 3)
            for (int i = 0; i < rank; ++i) readInt32(); // lower bounds
        uint8_t bt = readByte();
        int prim = 0;
        if (bt == BtPrimitive || bt == BtPrimitiveArray) prim = readByte();
        else if (bt == BtSystemClass) readString();
        else if (bt == BtClass) { readString(); readInt32(); }
        if (total < 0 || total > INT32_MAX) fail("bad array size");
        Value v;
        v.kind = Value::Ref;
        v.ref = o.id;
        readArrayElements(o, int(total), bt == BtPrimitive ? BtPrimitive : BtObject, prim);
        addObject(std::move(o));
        return v;
    }
    case MemberPrimitiveTyped: {
        Value v;
        v.kind = Value::Primitive;
        v.prim = readPrimitive(readByte());
        return v;
    }
    case MemberReference: {
        Value v;
        v.kind = Value::Ref;
        v.ref = readInt32();
        return v;
    }
    case ObjectNull: return Value{};
    case ObjectNullMultiple256: {
        Value v;
        v.nullCount = readByte();
        return v;
    }
    case ObjectNullMultiple: {
        Value v;
        v.nullCount = readInt32();
        return v;
    }
    case ArraySinglePrimitive: {
        Object o;
        o.id = readInt32();
        int len = readInt32();
        int prim = readByte();
        readArrayElements(o, len, BtPrimitive, prim);
        Value v;
        v.kind = Value::Ref;
        v.ref = o.id;
        addObject(std::move(o));
        return v;
    }
    case ArraySingleObject:
    case ArraySingleString: {
        Object o;
        o.id = readInt32();
        int len = readInt32();
        readArrayElements(o, len, BtObject, 0);
        Value v;
        v.kind = Value::Ref;
        v.ref = o.id;
        addObject(std::move(o));
        return v;
    }
    case MessageEnd:
        if (messageEnd) *messageEnd = true;
        return Value{};
    }
    fail("unsupported record type");
}

void Reader::parse(const QByteArray &data, qsizetype &pos)
{
    m_data = &data;
    m_pos = pos;
    for (;;) {
        bool end = false;
        readRecord(&end);
        if (end) break;
    }
    pos = m_pos;
}

const Object *Reader::object(int id) const
{
    auto it = m_objects.find(id);
    return it == m_objects.end() ? nullptr : &it->second;
}

const Value *Reader::member(const Object &o, const QString &name) const
{
    QString suffix = QLatin1Char('+') + name;
    for (int i = 0; i < o.memberNames.size() && size_t(i) < o.values.size(); ++i) {
        const QString &m = o.memberNames[i];
        if (m == name || m.endsWith(suffix)) return &o.values[size_t(i)];
    }
    return nullptr;
}

const Object *Reader::memberObject(const Object &o, const QString &name) const
{
    const Value *v = member(o, name);
    return v ? deref(*v) : nullptr;
}

QVariant Reader::memberPrim(const Object &o, const QString &name) const
{
    const Value *v = member(o, name);
    if (!v) return {};
    if (v->kind == Value::Primitive) return v->prim;
    // Enums are serialized as a class with a single "value__" member.
    if (const Object *e = deref(*v)) {
        if (e->kind == Object::String) return e->str;
        return memberPrim(*e, QStringLiteral("value__"));
    }
    return {};
}

QString Reader::memberString(const Object &o, const QString &name) const
{
    const Value *v = member(o, name);
    if (!v) return {};
    if (const Object *s = deref(*v); s && s->kind == Object::String) return s->str;
    if (v->kind == Value::Primitive) return v->prim.toString();
    return {};
}

} // namespace pane::nrbf
