#ifndef JYDEVICEADAPTER_H
#define JYDEVICEADAPTER_H

#include "jydevicetype.h"

#include <QString>
#include <memory>

class JYDeviceAdapter
{
public:
    virtual ~JYDeviceAdapter() = default;

    virtual JYDeviceKind kind() const = 0;
    virtual bool configure(const JYDeviceConfig &config, QString *error) = 0;
    virtual bool start(QString *error) = 0;
    virtual bool trigger(QString *error) = 0;
    virtual bool stop(QString *error) = 0;
    virtual bool close(QString *error) = 0;
    virtual bool read(JYDataPacket *out, QString *error) = 0;
};

std::unique_ptr<JYDeviceAdapter> createJY532xAdapter(JYDeviceKind kind);
std::unique_ptr<JYDeviceAdapter> createJY5711Adapter();
std::unique_ptr<JYDeviceAdapter> createJY8902Adapter();

#endif // JYDEVICEADAPTER_H
