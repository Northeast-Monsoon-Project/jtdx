#ifndef HAMLIB_UPDATE_SUPPORT_HPP
#define HAMLIB_UPDATE_SUPPORT_HPP
#include <QString>
namespace HamlibUpdateSupport {
bool validatePe(QString const& path, QString *error);
QString sha256(QString const& path);
bool atomicCopy(QString const& from, QString const& to, QString *error);
// The current DLL remains intact until the replacement file is fully written.
bool replaceWithBackup(QString const& candidate, QString const& active,
                       QString const& backup, QString *error);
}
#endif
