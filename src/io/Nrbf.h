#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <map>
#include <vector>

// A minimal reader for the .NET Remoting Binary Format ([MS-NRBF]), as written by
// BinaryFormatter. It builds a flat object table; values reference objects by id.
namespace pane::nrbf {

struct Value {
    enum Kind { Null, Primitive, Ref } kind = Null;
    QVariant prim;
    int ref = 0;
    int nullCount = 1; // for ObjectNullMultiple inside arrays
};

struct Object {
    enum Kind { Class, ObjectArray, PrimitiveArray, String } kind = Class;
    int id = 0;
    QString className;      // Class
    QStringList memberNames; // Class
    std::vector<Value> values; // Class members / ObjectArray elements
    QByteArray bytes;       // PrimitiveArray of Byte
    QVariantList prims;     // other PrimitiveArray
    QString str;            // String
};

class Reader {
public:
    // Parses records from data[pos] until MessageEnd; pos is advanced past it.
    // Throws std::runtime_error on malformed input.
    void parse(const QByteArray &data, qsizetype &pos);

    const std::map<int, Object> &objects() const { return m_objects; }
    // Object ids in the order their records appeared.
    const std::vector<int> &order() const { return m_order; }
    int rootId() const { return m_root; }

    const Object *object(int id) const;
    const Object *deref(const Value &v) const { return v.kind == Value::Ref ? object(v.ref) : nullptr; }
    // Finds a member whose name equals `name` or ends with "+name".
    const Value *member(const Object &o, const QString &name) const;
    const Object *memberObject(const Object &o, const QString &name) const;
    QVariant memberPrim(const Object &o, const QString &name) const;
    QString memberString(const Object &o, const QString &name) const;

private:
    struct ClassMeta {
        QString name;
        QStringList memberNames;
        std::vector<uint8_t> binaryTypes;
        std::vector<int> primTypes; // for Primitive / PrimitiveArray members
        bool typed = true;
    };

    Value readRecord(bool *messageEnd = nullptr);
    Value readClass(int id, const ClassMeta &meta);
    void readClassInfo(ClassMeta &m, int &id);
    void readMemberTypeInfo(ClassMeta &m);
    QVariant readPrimitive(int type);
    QString readString();
    int readInt32();
    uint8_t readByte();
    void need(qsizetype n);
    void addObject(Object o);
    void readArrayElements(Object &arr, int count, uint8_t binaryType, int primType);

    const QByteArray *m_data = nullptr;
    qsizetype m_pos = 0;
    std::map<int, Object> m_objects;
    std::map<int, ClassMeta> m_metas;
    std::vector<int> m_order;
    int m_root = 0;
};

} // namespace pane::nrbf
