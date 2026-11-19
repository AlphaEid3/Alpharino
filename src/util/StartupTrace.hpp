#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QString>

namespace chatterino {

// Appends one line next to the exe when ALPHARINO_STARTUP_TRACE is set.
// Used to see how far startup got before a stack overflow.
inline void startupTrace(const QString &stage)
{
    static const bool enabled =
        qEnvironmentVariableIsSet("ALPHARINO_STARTUP_TRACE");
    if (!enabled || stage.isEmpty() || QCoreApplication::instance() == nullptr)
    {
        return;
    }

    static QFile file(QDir(QCoreApplication::applicationDirPath())
                          .filePath(QStringLiteral("startup-trace.txt")));
    static const bool opened =
        file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
    if (!opened)
    {
        return;
    }

    file.write(stage.toUtf8());
    file.write("\n");
    file.flush();
}

}  // namespace chatterino
