#include "JtAlertCompatibility.hpp"

#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QTemporaryDir>

namespace {
bool fail(QString* error,QString const& message)
{
  if(error)*error=message;
  return false;
}

bool safeName(QString const& name)
{
  static QRegularExpression invalid(QStringLiteral("[\\x00-\\x1f\\x7f/\\\\:*?\"<>|]"));
  return !name.isEmpty() && name==name.trimmed() && !name.endsWith('.')
    && !name.contains("..") && !invalid.match(name).hasMatch()
    && JtAlertCompatibility::instanceId(name).size()<=240;
}

bool safeDirectory(QString path)
{
  // Checking every ancestor also rejects root/link/subdirectory, where the
  // immediate target is not itself a symlink. Missing ancestors may be made.
  for(;;) {
    QFileInfo info(path);
    if(info.isSymLink() || (info.exists() && !info.isDir()))return false;
    QString parent=info.dir().absolutePath();
    if(parent==path)return true;
    path=parent;
  }
}

bool ownedOrAbsent(QString const& path,QString const& applicationName,
                   QString const& source,QString* error)
{
  QFileInfo info(path);
  if(info.isSymLink() || (info.exists() && !info.isFile()))
    return fail(error,QStringLiteral("Unsafe JTAlert discovery file: %1").arg(path));
  if(!info.exists())return true;
  QSettings existing(path,QSettings::IniFormat);
  existing.beginGroup("JTDXFCompatibility");
  bool owned=existing.value("format").toInt()==1
    && existing.value("applicationName").toString()==applicationName
    && existing.value("sourceSettingsPath").toString()==source;
  if(existing.status()!=QSettings::NoError || !owned)
    return fail(error,QStringLiteral("JTAlert discovery file belongs to another configuration: %1").arg(path));
  return true;
}
}

QString JtAlertCompatibility::instanceId(QString const& applicationName)
{
  return QStringLiteral("JTDX - ")+applicationName;
}

QString JtAlertCompatibility::windowTitle(QString const& applicationName,QString const& version,
                                        QString const& revision)
{
  QString title=instanceId(applicationName)+" v"+version+"  JTDX-F by Fluxmancer";
  if(!revision.isEmpty())title+=' '+revision;
  return title+", derivative work based on WSJT-X by K1JT";
}

bool JtAlertCompatibility::writeDiscovery(QDir const& genericConfigRoot,QString applicationName,
                                        QString sourceSettingsPath,QVariantMap const& configuration,
                                        QString* error)
{
  if(error)error->clear();
  if(!safeName(applicationName) || sourceSettingsPath.isEmpty())
    return fail(error,QStringLiteral("Invalid JTAlert discovery identity or source settings path"));
  auto root=QDir::cleanPath(genericConfigRoot.absolutePath());
  auto source=QDir::cleanPath(QFileInfo(sourceSettingsPath).absoluteFilePath());
  auto id=instanceId(applicationName);
  auto directory=QDir(root).absoluteFilePath(id);
  auto path=QDir(directory).absoluteFilePath(id+".ini");
  if(path==source || !safeDirectory(directory))
    return fail(error,QStringLiteral("Unsafe JTAlert discovery directory: %1").arg(directory));
  if(!QDir().mkpath(directory) || !safeDirectory(directory))
    return fail(error,QStringLiteral("Cannot create JTAlert discovery directory: %1").arg(directory));
  auto lockPath=path+".lock";
  if(QFileInfo(lockPath).isSymLink())
    return fail(error,QStringLiteral("Unsafe JTAlert discovery lock: %1").arg(lockPath));
  QLockFile lock(lockPath);
  if(!lock.tryLock(0))
    return fail(error,QStringLiteral("JTAlert discovery file is locked: %1").arg(path));
  if(!ownedOrAbsent(path,applicationName,source,error))return false;

  // QSettings supplies the same INI escaping as the main settings, but only
  // this explicit projection is serialized. The main settings are never read.
  // A QTemporaryFile can retain its Windows handle after close(), preventing
  // QSettings from atomically replacing it. Own a temporary directory instead
  // so QSettings is the sole owner of its serialization file and its handles.
  QTemporaryDir temporary(QDir(directory).absoluteFilePath(".jtdxf-discovery-XXXXXX"));
  if(!temporary.isValid())return fail(error,temporary.errorString());
  auto temporaryPath=temporary.filePath("projection.ini");
  {
    QSettings projection(temporaryPath,QSettings::IniFormat);
    projection.beginGroup("Configuration");
    for(auto key:{"UDPServer","UDPServerPort","AcceptUDPRequests","MyCall","MyGrid"})
      if(configuration.contains(key))projection.setValue(key,configuration.value(key));
    projection.endGroup();
    projection.beginGroup("JTDXFCompatibility");
    projection.setValue("format",1);
    projection.setValue("applicationName",applicationName);
    projection.setValue("sourceSettingsPath",source);
    projection.endGroup();projection.sync();
    if(projection.status()!=QSettings::NoError)
      return fail(error,QStringLiteral("Cannot serialize JTAlert discovery settings: %1").arg(path));
  }
  QFile serialized(temporaryPath);
  if(!serialized.open(QIODevice::ReadOnly))return fail(error,serialized.errorString());
  auto data=serialized.readAll();
  if(serialized.error()!=QFileDevice::NoError)return fail(error,serialized.errorString());
  serialized.close();
  // Recheck immediately before replacement. QSaveFile's direct-write fallback
  // stays disabled, so an unsuccessful save cannot truncate an existing file.
  if(!safeDirectory(directory) || !ownedOrAbsent(path,applicationName,source,error))
    return fail(error,QStringLiteral("JTAlert discovery destination changed: %1").arg(path));
  QSaveFile output(path);
  output.setDirectWriteFallback(false);
  if(!output.open(QIODevice::WriteOnly))return fail(error,output.errorString());
  if(!output.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner))
    return fail(error,output.errorString());
  if(output.write(data)!=data.size())return fail(error,output.errorString());
  if(!output.commit())return fail(error,output.errorString());
  return true;
}
