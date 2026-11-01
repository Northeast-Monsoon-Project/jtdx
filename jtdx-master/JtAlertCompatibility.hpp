#ifndef JTDXF_JTALERT_COMPATIBILITY_HPP
#define JTDXF_JTALERT_COMPATIBILITY_HPP

#include <QDir>
#include <QString>
#include <QVariantMap>

// The discovery identity is deliberately separate from the real application
// name, which still owns settings, logs, process locks and shared memory.
namespace JtAlertCompatibility {
QString instanceId(QString const& applicationName);
QString windowTitle(QString const& applicationName,QString const& version,QString const& revision);
bool writeDiscovery(QDir const& genericConfigRoot,QString applicationName,
                    QString sourceSettingsPath,QVariantMap const& configuration,
                    QString* error=nullptr);
}

#endif
