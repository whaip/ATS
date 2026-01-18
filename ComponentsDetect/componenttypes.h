#ifndef COMPONENTTYPES_H
#define COMPONENTTYPES_H
#include <QString>
#include <QMap>
#include <QMetaType>

struct CompLabel {
    int id;                      // 标签ID
    double x;                       // 边界框左上角X坐标
    double y;                       // 边界框左上角Y坐标
    double w;                       // 边界框宽度
    double h;                       // 边界框高度
    int cls;                     // 类别ID
    double confidence;           // 置信度 [0.0, 1.0]
    QString label;               // 标签文本
    QString position_number;     // 位置编号
    QByteArray notes;           // 备注信息

    CompLabel(int id, double x, double y, double w, double h, int cls, double confidence, const QString &label, const QString &position_number, const QByteArray &notes)
        : id(id), x(x), y(y), w(w), h(h), cls(cls), confidence(confidence), label(label), position_number(position_number), notes(notes) {}
};
Q_DECLARE_METATYPE(CompLabel)
#endif // COMPONENTTYPES_H
