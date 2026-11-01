#include "HamlibUpdateSupport.hpp"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QtEndian>
namespace HamlibUpdateSupport {
bool validatePe(QString const& path, QString *error) {
  QFile f(path);
  auto fail=[error](QString const& why) { if(error)*error=why; return false; };
  if(!f.open(QIODevice::ReadOnly)) return fail(f.errorString());
  if(f.size()<512 || f.size()>32*1024*1024) return fail(QStringLiteral("Invalid DLL size"));
  auto dos=f.read(64);
  if(dos.size()!=64 || dos.left(2)!="MZ") return fail(QStringLiteral("Not a Windows DLL"));
  auto offset=qFromLittleEndian<quint32>(reinterpret_cast<uchar const*>(dos.constData()+60));
  if(offset>quint64(f.size()-26) || !f.seek(offset)) return fail(QStringLiteral("Invalid PE offset"));
  auto pe=f.read(26);
  if(pe.size()!=26)return fail(QStringLiteral("Truncated PE header"));
  auto u16=[&pe](int n){return qFromLittleEndian<quint16>(reinterpret_cast<uchar const*>(pe.constData()+n));};
  if(pe.left(4)!=QByteArray("PE\0\0",4) || !(u16(22)&0x2000)) return fail(QStringLiteral("Not a PE DLL"));
  if(u16(4)!=0x8664 || u16(24)!=0x20b) return fail(QStringLiteral("A 64-bit x86 Hamlib DLL is required"));
  return true;
}
QString sha256(QString const& path) {
  QFile f(path); if(!f.open(QIODevice::ReadOnly))return {};
  QCryptographicHash h(QCryptographicHash::Sha256); if(!h.addData(&f))return {};
  return QString::fromLatin1(h.result().toHex());
}
bool atomicCopy(QString const& from,QString const& to,QString *error) {
  QFile input(from); QSaveFile output(to); output.setDirectWriteFallback(false);
  auto fail=[error,&from,&to](QString const& why){if(error)*error=QStringLiteral("%1 -> %2: %3").arg(from,to,why);return false;};
  if(!input.open(QIODevice::ReadOnly))return fail(input.errorString());
  if(!output.open(QIODevice::WriteOnly))return fail(output.errorString());
  while(!input.atEnd()) {
    auto data=input.read(1024*1024);
    if(data.isEmpty() && input.error()!=QFile::NoError)return fail(input.errorString());
    if(output.write(data)!=data.size())return fail(output.errorString());
  }
  if(!output.commit())return fail(output.errorString());
  return true;
}
bool replaceWithBackup(QString const& candidate,QString const& active,QString const& backup,QString *error) {
  // Stage both versions before changing either durable pathname. QSaveFile
  // prevents partial active DLLs; a failed replacement restores the old backup.
  QTemporaryDir staged(QFileInfo(active).absolutePath()+"/.hamlib-swap-XXXXXX");
  if(!staged.isValid()){if(error)*error=QStringLiteral("Cannot stage the Hamlib transaction");return false;}
  auto temporary=staged.filePath("candidate.dll"), savedBackup=staged.filePath("previous-backup.dll");
  bool hadBackup=QFileInfo::exists(backup);
  if(!atomicCopy(candidate,temporary,error))return false;
  if(hadBackup && !atomicCopy(backup,savedBackup,error))return false;
  if(!atomicCopy(active,backup,error))return false;
  if(atomicCopy(temporary,active,error))return true;
  QString restoreError;
  if(hadBackup) {
    if(!atomicCopy(savedBackup,backup,&restoreError)) {
      staged.setAutoRemove(false);
      if(error)*error+=QStringLiteral("; previous backup preserved at ")+savedBackup;
    }
  } else QFile::remove(backup);
  return false;
}
}
