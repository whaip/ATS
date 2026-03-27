#include "tpsruntimecontext.h"

namespace {
TPSPortBinding parseBinding(const QVariantMap &map)
{
    TPSPortBinding binding;
    binding.identifier = map.value(QStringLiteral("identifier")).toString();
    binding.type = static_cast<TPSPortType>(
        map.value(QStringLiteral("type"), static_cast<int>(TPSPortType::CurrentOutput)).toInt());
    binding.deviceKind = static_cast<JYDeviceKind>(
        map.value(QStringLiteral("deviceKind"), static_cast<int>(JYDeviceKind::PXIe5322)).toInt());
    binding.channel = map.value(QStringLiteral("channel")).toInt();
    binding.resourceId = map.value(QStringLiteral("resourceId")).toString();
    return binding;
}
}

QString TPSRuntimeContext::allocatedBindingsKey()
{
    return QStringLiteral("__allocatedBindings");
}

QVariantList TPSRuntimeContext::encodeBindings(const QVector<TPSPortBinding> &bindings)
{
    QVariantList list;
    list.reserve(bindings.size());
    for (const auto &binding : bindings) {
        QVariantMap map;
        map.insert(QStringLiteral("identifier"), binding.identifier);
        map.insert(QStringLiteral("type"), static_cast<int>(binding.type));
        map.insert(QStringLiteral("deviceKind"), static_cast<int>(binding.deviceKind));
        map.insert(QStringLiteral("channel"), binding.channel);
        map.insert(QStringLiteral("resourceId"), binding.resourceId);
        list.push_back(map);
    }
    return list;
}

QVector<TPSPortBinding> TPSRuntimeContext::decodeBindings(const QVariant &value)
{
    QVector<TPSPortBinding> bindings;
    const QVariantList list = value.toList();
    bindings.reserve(list.size());
    for (const QVariant &item : list) {
        const QVariantMap map = item.toMap();
        if (map.isEmpty()) {
            continue;
        }
        bindings.push_back(parseBinding(map));
    }
    return bindings;
}
