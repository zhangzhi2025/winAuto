#pragma once
// core 用 UTF-8 std::string，Qt 用 QString；转换只在这里做。
#include <QString>
#include <string>

inline QString qs(const std::string& s) { return QString::fromUtf8(s.data(), static_cast<int>(s.size())); }
inline std::string stdStr(const QString& s) {
    const QByteArray b = s.toUtf8();
    return std::string(b.constData(), static_cast<size_t>(b.size()));
}
