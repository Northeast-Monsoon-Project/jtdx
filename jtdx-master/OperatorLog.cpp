#include "OperatorLog.hpp"
#include "StationProfiles.hpp"
#include "logbook/adif.h"
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>
#include <QTime>
#include <algorithm>
#include <cerrno>
#include <cstring>
#ifdef Q_OS_WIN
#include <io.h>
#else
#include <unistd.h>
#endif
namespace {
QList<OperatorLog::Record> readBytes(QString const& path,QByteArray const& bytes,QString* error,
                                    bool externalImport=false);
QString pathKey(QString path) {
#ifdef Q_OS_WIN
  return path.toCaseFolded();
#else
  return path;
#endif
}
QString changedError() {
  return QCoreApplication::translate("OperatorLog","Log files changed during processing; originals retained. Please retry.");
}
QString invalidQsoError() {
  return QCoreApplication::translate("OperatorLog","Invalid QSO for the current station; originals retained.");
}
QString header() { return "JTDX-F operator log\n<ADIF_VER:5>3.1.8 <PROGRAMID:6>JTDX-F <EOH>\n"; }
bool save(QString const& path,QByteArray const& bytes,QString *error) {
  QSaveFile f(path);
  if(!f.open(QIODevice::WriteOnly) || f.write(bytes)!=bytes.size() || !f.commit()) {
    if(error) *error=f.errorString();
    return false;
  }
  return true;
}
bool snapshot(QString const& path,QByteArray* bytes,QString* error) {
  QFile file(path);
  if(!file.open(QIODevice::ReadOnly)){*error=file.errorString();return false;}
  *bytes=file.readAll();
  if(file.error()!=QFileDevice::NoError){*error=file.errorString();return false;}
  return true;
}
bool unchanged(QString const& path,QByteArray const& expected,QString* error) {
  QByteArray current;
  if(!snapshot(path,&current,error))return false;
  if(current!=expected){*error=changedError();return false;}
  return true;
}
bool archiveOriginal(QDir dir,QString const& path,QByteArray const& bytes,bool mixed,QString* error) {
  if(!dir.mkpath("old") || !dir.cd("old")){
    *error=QCoreApplication::translate("OperatorLog","Cannot create the old backup directory; originals retained.");return false;
  }
  auto info=QFileInfo(path);auto digest=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
  auto hashed=info.completeBaseName()+(mixed?"-mixed-":"-")+digest+'.'+info.suffix();
  auto dest=dir.filePath(mixed?hashed:info.fileName());
  if(QFile::exists(dest)) {
    QByteArray prior;
    if(!snapshot(dest,&prior,error))return false;
    if(prior==bytes)return true;
    dest=dir.filePath(hashed);
    if(QFile::exists(dest)) {
      if(!snapshot(dest,&prior,error))return false;
      if(prior==bytes)return true;
      *error=QCoreApplication::translate("OperatorLog","Backup contents differ; originals retained.");return false;
    }
  }
  return save(dest,bytes,error);
}
QString keyField(OperatorLog::Record const& r,QString key) { return r.value(key).trimmed().toUpper(); }
void fillMissing(OperatorLog::Record& target,OperatorLog::Record const& source) {
  for(auto i=source.begin();i!=source.end();++i)
    if(target.value(i.key()).isEmpty())target.insert(i.key(),i.value());
}
bool saveRecords(QString const& path,QList<OperatorLog::Record> const& records,QString* error) {
  QString body=header();for(auto const& record:records)body+=OperatorLog::serialize(record);
  auto bytes=body.toUtf8();QFile existing(path);
  if(existing.open(QIODevice::ReadOnly) && existing.readAll()==bytes)return true;
  existing.close(); // Windows cannot atomically replace our own open read handle.
  return save(path,bytes,error);
}

// Only a handful of recent appends need revision. Remember their byte ranges,
// not an index of every historical QSO. Verify those bytes on every use because
// an external logger need not honor our directory lock.
struct QsoPatch {
  QString path;
  qint64 offset=0,total=0;
  QByteArray before,after,anchor;
  bool tail=false;
};
QMutex patchCacheMutex;
QMap<QString,QsoPatch> patchCache;
QString patchKey(QString const& path,QString const& id) {
  return QFileInfo(path).absoluteFilePath()+'\n'+id;
}
QByteArray anchorAt(QFile& file,qint64 offset) {
  auto length=qMin(qint64(128),offset);
  if(!file.seek(offset-length))return {};
  return file.read(length);
}
bool currentPatch(QsoPatch const& patch,QString* error) {
  QFile file(patch.path);
  if(!file.open(QIODevice::ReadOnly)){*error=file.errorString();return false;}
  if(file.size()!=patch.total || anchorAt(file,patch.offset)!=patch.anchor
     || !file.seek(patch.offset) || file.read(patch.before.size())!=patch.before) {
    *error=changedError();return false;
  }
  return true;
}
void rememberPatch(QString const& id,QsoPatch const& patch) {
  QMutexLocker guard(&patchCacheMutex);
  if(patchCache.size()>=32)patchCache.erase(patchCache.begin());
  patchCache.insert(patchKey(patch.path,id),patch);
}
bool locateQso(QString const& path,QString const& id,QString const& call,
               QsoPatch* patch,OperatorLog::Record* record,QString* error) {
  {
    QMutexLocker guard(&patchCacheMutex);
    auto cached=patchCache.constFind(patchKey(path,id));
    if(cached!=patchCache.cend() && currentPatch(cached.value(),error)) {
      *patch=cached.value();auto parsed=OperatorLog::parse(QString::fromUtf8(patch->before),error);
      if(error->isEmpty() && parsed.size()==1 && OperatorLog::identity(parsed.first(),call)==id) {
        *record=parsed.first();return true;
      }
    }
    error->clear();
  }
  QByteArray bytes;
  if(!snapshot(path,&bytes,error))return false;
  auto text=QString::fromUtf8(bytes);
  auto records=OperatorLog::parse(text,error);
  if(!error->isEmpty())return false;
  int matches=0,index=0,pos=0,start=-1,previous=0; qint64 bytePosition=0;
  // The validated ADIF field lengths skip embedded <EOR> text and Unicode
  // values. Regex searches for a record terminator would corrupt such fields.
  while(pos<text.size()) {
    int tag=text.indexOf('<',pos);if(tag<0)break;
    int end=text.indexOf('>',tag+1);auto parts=text.mid(tag+1,end-tag-1).split(':');
    auto name=parts.first().trimmed().toUpper();pos=end+1;
    if(name=="EOH"){start=-1;continue;}
    if(name=="EOR") {
      if(start<0)continue;
      while(pos<text.size() && text.at(pos).isSpace())++pos;
      bytePosition+=text.mid(previous,start-previous).toUtf8().size();
      auto body=text.mid(start,pos-start).toUtf8();
      if(index<records.size() && OperatorLog::identity(records.at(index),call)==id) {
        ++matches;*record=records.at(index);patch->path=path;patch->offset=bytePosition;
        patch->total=bytes.size();patch->before=body;patch->tail=bytePosition+body.size()==bytes.size();
        patch->anchor=bytes.mid(qMax(qint64(0),bytePosition-128),qMin(qint64(128),bytePosition));
      }
      ++index;bytePosition+=body.size();previous=pos;start=-1;continue;
    }
    if(start<0)start=tag;
    pos+=parts.at(1).toInt();
  }
  if(matches!=1){*error=matches?QObject::tr("Ambiguous QSO identity; log unchanged"):QObject::tr("Recorded QSO not found; log unchanged");return false;}
  return true;
}

QString updateJournal(QDir const& dir) {return dir.filePath(".operator-log-update.json");}
QJsonObject patchJson(QsoPatch const& patch) {
  return {{"file",QFileInfo(patch.path).fileName()},{"offset",QString::number(patch.offset)},
    {"total",QString::number(patch.total)},{"before",QString::fromLatin1(patch.before.toBase64())},
    {"after",QString::fromLatin1(patch.after.toBase64())},{"anchor",QString::fromLatin1(patch.anchor.toBase64())},
    {"tail",patch.tail}};
}
// 0 = untouched, 1 = committed, 2 = interrupted tail write; -1 = conflict.
int patchState(QsoPatch const& patch,QString* error) {
  QFileInfo info(patch.path);QFile file(patch.path);
  if(info.isSymLink() || !info.isFile() || !file.open(QIODevice::ReadOnly)) {
    *error=file.errorString().isEmpty()?changedError():file.errorString();return -1;
  }
  if(anchorAt(file,patch.offset)!=patch.anchor || !file.seek(patch.offset)) {*error=changedError();return -1;}
  qint64 afterTotal=patch.total-patch.before.size()+patch.after.size();
  if(file.size()==patch.total && file.read(patch.before.size())==patch.before)return 0;
  if(!file.seek(patch.offset)){*error=file.errorString();return -1;}
  if(file.size()==afterTotal && file.read(patch.after.size())==patch.after)return 1;
  if(patch.tail && file.size()>=patch.offset && file.size()<=qMax(patch.total,afterTotal)
     && file.seek(patch.offset)) {
    auto current=file.readAll();int prefix=0;
    while(prefix<qMin(current.size(),patch.after.size()) && current.at(prefix)==patch.after.at(prefix))++prefix;
    if(current.size()==qMax(patch.before.size(),prefix) && current.mid(prefix)==patch.before.mid(prefix))return 2;
  }
  *error=changedError();return -1;
}
bool flushUpdatedQso(QFile& file,QString* error) {
  if(!file.flush()){*error=file.errorString();return false;}
  int result;
  do {
#ifdef Q_OS_WIN
    result=::_commit(file.handle());
#else
    result=::fsync(file.handle());
#endif
  } while(result<0 && errno==EINTR);
  if(result<0){*error=QString::fromLocal8Bit(std::strerror(errno));return false;}
  return true;
}
bool applyPatch(QsoPatch const& patch,QString* error) {
  auto state=patchState(patch,error);if(state<0)return false;
  if(state==1) {
    // A previous attempt may have written every byte but failed to flush it.
    QFile file(patch.path);
    if(!file.open(QIODevice::ReadWrite)){*error=file.errorString();return false;}
    return flushUpdatedQso(file,error);
  }
  if(patch.tail) {
    QFile file(patch.path);
    if(!file.open(QIODevice::ReadWrite) || !file.seek(patch.offset)
       || file.write(patch.after)!=patch.after.size()
       || !file.resize(patch.offset+patch.after.size())) {
      *error=file.errorString();return false;
    }
    return flushUpdatedQso(file,error);
  }
  // An external append or an older selected QSO takes the slower atomic path.
  // Preserve all unrelated bytes; no other quarterly logs are read or merged.
  QByteArray bytes;if(!snapshot(patch.path,&bytes,error))return false;
  if(bytes.size()!=patch.total || bytes.mid(patch.offset,patch.before.size())!=patch.before) {
    *error=changedError();return false;
  }
  bytes.replace(patch.offset,patch.before.size(),patch.after);
  return save(patch.path,bytes,error);
}
bool recoverUpdate(QDir dir,QString const& call,QString* error) {
  auto path=updateJournal(dir);if(!QFile::exists(path))return true;
  QFile journal(path);
  if(!journal.open(QIODevice::ReadOnly) || journal.size()>8*1024*1024) {*error=QObject::tr("Cannot read pending log update");return false;}
  QJsonParseError jsonError;auto document=QJsonDocument::fromJson(journal.readAll(),&jsonError);journal.close();
  auto object=document.object();auto entries=object.value("patches").toArray();
  QList<QsoPatch> patches;QSet<QString> names;QString id;
  if(jsonError.error!=QJsonParseError::NoError || object.value("version").toInt()!=1
     || object.value("station").toString()!=call || entries.size()!=2) {
    *error=QObject::tr("Invalid pending log update; originals retained");return false;
  }
  for(auto const& entry:entries) {
    auto value=entry.toObject();auto name=value.value("file").toString();
    bool offsetOk=false,totalOk=false;QsoPatch patch;
    patch.path=dir.filePath(name);patch.offset=value.value("offset").toString().toLongLong(&offsetOk);
    patch.total=value.value("total").toString().toLongLong(&totalOk);
    patch.before=QByteArray::fromBase64(value.value("before").toString().toLatin1());
    patch.after=QByteArray::fromBase64(value.value("after").toString().toLatin1());
    patch.anchor=QByteArray::fromBase64(value.value("anchor").toString().toLatin1());patch.tail=value.value("tail").toBool();
    auto before=OperatorLog::parse(QString::fromUtf8(patch.before),error);
    if(!error->isEmpty())return false;
    auto after=OperatorLog::parse(QString::fromUtf8(patch.after),error);
    if(!error->isEmpty())return false;
    if(!offsetOk || !totalOk || patch.offset<0 || patch.total<patch.offset
       || patch.before.size()>patch.total-patch.offset || patch.anchor.size()!=qMin(qint64(128),patch.offset)
       || (patch.tail && patch.offset+patch.before.size()!=patch.total)
       || name!=QFileInfo(name).fileName() || name.contains('/') || name.contains('\\')
       || names.contains(name) || before.size()!=1 || after.size()!=1
       || OperatorLog::identity(before.first(),call)!=OperatorLog::identity(after.first(),call)
       || StationProfiles::normalize(after.first().value("STATION_CALLSIGN"))!=call) {
      *error=QObject::tr("Invalid pending log update; originals retained");return false;
    }
    auto date=QDate::fromString(after.first().value("QSO_DATE"),"yyyyMMdd");
    auto quarter=StationProfiles::folder(call)+QString::number(date.year())+"S"+QString::number((date.month()-1)/3+1)+".adi";
    auto nextId=OperatorLog::identity(after.first(),call);
    if(!date.isValid() || (name!="wsjtx_log.adi" && name.compare(quarter,Qt::CaseInsensitive)!=0)
       || (!id.isEmpty() && id!=nextId)) {*error=QObject::tr("Invalid pending log update; originals retained");return false;}
    id=nextId;names.insert(name);patches.append(patch);
  }
  if(!names.contains("wsjtx_log.adi")) {*error=QObject::tr("Invalid pending log update; originals retained");return false;}
  // Check both copies before modifying either, including recovery after a
  // crash. A conflicting external edit leaves the journal and originals intact.
  for(auto const& patch:patches)if(patchState(patch,error)<0)return false;
  for(auto const& patch:patches) {
    if(!applyPatch(patch,error))return false;
    auto cached=patch;cached.total+=cached.after.size()-cached.before.size();cached.before=cached.after;cached.after.clear();
    rememberPatch(id,cached);
  }
  if(!QFile::remove(path)){*error=QObject::tr("Cannot remove completed log update journal");return false;}
  return true;
}
OperatorLog::ImportResult synchronizeRecords(QString const& call,QDir base) {
  using namespace OperatorLog;
  ImportResult result;QMap<QString,Record> merged;QSet<QString> primaryIds;
  StationProfile profile;profile.call=call;profile.grid="AA00";
  auto validation=StationProfiles::validate(profile);
  if(!validation.isEmpty()){result.errors.append(validation);return result;}
  QString recoveryError;
  if(!recoverUpdate(directory(call,base),call,&recoveryError)){result.errors.append(recoveryError);return result;}
  auto primary=files(call,base);auto full=compatibilityFile(call,base);
  QStringList sources=primary;if(QFile::exists(full))sources.append(full);
  QMap<QString,QByteArray> originals;QSet<QString> mixed;
  for(auto const& path:sources) {
    QString error;QByteArray bytes;
    if(!snapshot(path,&bytes,&error)){result.errors.append(path+": "+error);return result;}
    auto records=readBytes(path,bytes,&error);
    if(!error.isEmpty()){result.errors.append(path+": "+error);return result;}
    originals.insert(path,bytes);
    for(auto record:records) {
      auto station=StationProfiles::normalize(record.value("STATION_CALLSIGN"));
      if(station.isEmpty())station=call;
      // CALL is the contacted station; OPERATOR may differ at a club station.
      // Reject foreign ownership before validating that foreign QSO's fields.
      if(station!=call){++result.skipped;mixed.insert(path);continue;}
      if(record.value("CALL").trimmed().isEmpty() ||
         !QDate::fromString(record.value("QSO_DATE"),"yyyyMMdd").isValid()) {
        result.errors.append(path+": "+invalidQsoError());
        return result;
      }
      record["STATION_CALLSIGN"]=call;
      auto id=identity(record,call);
      if(path!=full)primaryIds.insert(id);
      if(merged.contains(id))fillMissing(merged[id],record);else merged.insert(id,record);
    }
  }
  // Build the sort key once. The identity includes normalization and band
  // lookup, so recomputing it in every comparator is expensive on large logs.
  QList<QPair<QString,Record>> sorted;sorted.reserve(merged.size());
  for(auto i=merged.cbegin();i!=merged.cend();++i)
    sorted.append({i.value().value("QSO_DATE")+i.value().value("TIME_ON").leftJustified(6,'0')+i.key(),i.value()});
  std::sort(sorted.begin(),sorted.end(),[](QPair<QString,Record> const& a,QPair<QString,Record> const& b) {return a.first<b.first;});
  QList<Record> records;records.reserve(sorted.size());
  for(auto const& item:sorted)records.append(item.second);
  QMap<QString,QList<Record>> groups;
  QMap<QString,QString> existingPaths;
  for(auto const& path:primary){groups.insert(path,{});existingPaths.insert(pathKey(path),path);}
  auto dir=directory(call,base);auto folder=StationProfiles::folder(call);
  for(auto const& record:records) {
    auto date=QDate::fromString(record.value("QSO_DATE"),"yyyyMMdd");
    // The directory is already present; avoid a filesystem mkdir/stat and
    // callsign validation for each individual contact.
    auto path=dir.filePath(folder+QString::number(date.year())+"S"+QString::number((date.month()-1)/3+1)+".adi");
    path=existingPaths.value(pathKey(path),path);
    groups[path].append(record);
  }
  // No working file changes until every contaminated original is backed up.
  QString error;
  for(auto const& path:mixed) {
    if(!archiveOriginal(dir,path,originals.value(path),true,&error)) {
      result.errors.append(path+": "+error);return result;
    }
    ++result.archived;
  }
  auto currentSources=files(call,base);if(QFile::exists(full))currentSources.append(full);
  if(currentSources!=sources){result.errors.append(changedError());return result;}
  for(auto i=originals.cbegin();i!=originals.cend();++i)
    if(!unchanged(i.key(),i.value(),&error)){result.errors.append(i.key()+": "+error);return result;}
  // Read and validate every input before writing. Atomic per-file commits and
  // the union on retry recover an interrupted multi-file synchronization.
  auto stillCurrent=[&](QString const& path) {
    if(originals.contains(path))return unchanged(path,originals.value(path),&error);
    if(!QFile::exists(path))return true;
    error=changedError();return false;
  };
  for(auto group=groups.begin();group!=groups.end();++group)
    if(!stillCurrent(group.key()) || !saveRecords(group.key(),group.value(),&error)) {
      result.errors.append(group.key()+": "+error);return result;
    }
  if(!stillCurrent(full) || !saveRecords(full,records,&error))result.errors.append(full+": "+error);
  result.imported=merged.size()-primaryIds.size();
  return result;
}
}
QDir OperatorLog::root() { return QDir(QStandardPaths::writableLocation(QStandardPaths::DataLocation)); }
QDir OperatorLog::directory(QString const& call,QDir base) {
  QString name=StationProfiles::folder(call);
  static const QRegularExpression invalidName("[^A-Z0-9_]");
  if(name.isEmpty() || name.contains(invalidName)) return base;
  base.mkpath(name);return QDir(base.filePath(name));
}
QString OperatorLog::quarterFile(QString const& call,QDate date,QDir base) {
  if(call.isEmpty() || !date.isValid()) return {};
  return directory(call,base).filePath(StationProfiles::folder(call)+QString::number(date.year())+"S"+QString::number((date.month()-1)/3+1)+".adi");
}
bool OperatorLog::isQuarterFile(QString const& path) {
  static const QRegularExpression pattern("^[A-Z0-9_]+[0-9]{4}S[1-4]\\.adi$",QRegularExpression::CaseInsensitiveOption);
  return pattern.match(QFileInfo(path).fileName()).hasMatch();
}
QStringList OperatorLog::files(QString const& call,QDir base) {
  if(call.isEmpty()) return {};
  auto dir=directory(call,base);QStringList out;
  for(auto const& f:dir.entryInfoList(QDir::Files,QDir::Name))
    if(isQuarterFile(f.fileName())) out.append(f.absoluteFilePath());
  return out;
}
QString OperatorLog::compatibilityFile(QString const& call,QDir base) {
  return call.isEmpty()?QString():directory(call,base).filePath("wsjtx_log.adi");
}
OperatorLog::ImportResult OperatorLog::synchronize(QString const& callsign,QDir base) {
  auto call=StationProfiles::normalize(callsign);ImportResult result;
  if(call.isEmpty())return result;
  QLockFile lock(directory(call,base).filePath(".operator-log.lock"));
  if(!lock.tryLock(10000)){result.errors.append("Log is busy; retry after import finishes");return result;}
  return synchronizeRecords(call,base);
}
OperatorLog::ImportResult OperatorLog::detail::synchronizeLocked(QString const& call,QDir base) {
  if(call.isEmpty())return {};
  return synchronizeRecords(call,base);
}
bool OperatorLog::detail::recoverPendingUpdateLocked(QString const& call,QDir base,QString* error) {
  QString localError;if(!error)error=&localError;error->clear();
  return call.isEmpty() || recoverUpdate(directory(call,base),StationProfiles::normalize(call),error);
}
QList<OperatorLog::Record> OperatorLog::parse(QString const& text,QString *error) {
  QList<Record> out;Record record;int pos=0;bool headerSeen=false;
  static const QRegularExpression fieldName("^[A-Z0-9_]+$");
  if(error) error->clear();
  while(pos<text.size()) {
    int start=text.indexOf('<',pos);if(start<0) break;
    int end=text.indexOf('>',start+1);
    if(end<0) { if(error) *error="Incomplete ADIF field header";return {}; }
    auto parts=text.mid(start+1,end-start-1).split(':');QString name=parts[0].trimmed().toUpper();
    pos=end+1;
    if(name=="EOH") {headerSeen=true;record.clear();continue;}
    if(name=="EOR") {if(!record.isEmpty()) out.append(record);record.clear();continue;}
    if(parts.size()<2 || parts.size()>3 || !fieldName.match(name).hasMatch()) {
      if(error) *error="Invalid ADIF field: "+name;
      return {};
    }
    bool ok=false;int n=parts[1].toInt(&ok);
    if(!ok || n<0 || n>text.size()-pos) {if(error) *error="Truncated ADIF value: "+name;return {};}
    record.insert(name,text.mid(pos,n));pos+=n;
  }
  if(!record.isEmpty()) {if(error) *error="ADIF record is missing <EOR>";return {};}
  if(out.isEmpty() && !headerSeen && !text.trimmed().isEmpty()) {if(error)*error="Not an ADIF log";return {};}
  return out;
}
QList<OperatorLog::Record> OperatorLog::read(QString const& path,QString *error) {
  if(error) error->clear();
  QString ioError;QByteArray bytes;
  if(!snapshot(path,&bytes,&ioError)){if(error)*error=ioError;return {};}
  return readBytes(path,bytes,error);
}
namespace {
// External ADIF writers differ on UTF-8 byte versus character lengths. Only
// repair an unambiguous byte-count endpoint when the character endpoint would
// split a real ADIF tag (or exceed EOF). Never use this for working files or
// update journals: their character lengths also define exact patch offsets.
bool splitsAdifTag(QString const& text,int start,int end) {
  if(end==start)return false;
  auto opening=text.lastIndexOf('<',end-1);
  if(opening<start)return false;
  auto closing=text.indexOf('>',opening+1);
  if(closing<end)return false;
  static const QRegularExpression descriptor(
    "\\A(?:EO[HR]|[A-Z][A-Z0-9_]*:[0-9]+(?::[A-Z])?)\\z",
    QRegularExpression::CaseInsensitiveOption);
  return descriptor.match(text.mid(opening+1,closing-opening-1)).hasMatch();
}
QString normalizeImportedAdif(QString const& text,QString* error) {
  QString normalized;int pos=0;
  while(pos<text.size()) {
    auto start=text.indexOf('<',pos);
    if(start<0){normalized+=text.mid(pos);break;}
    normalized+=text.mid(pos,start-pos);
    auto end=text.indexOf('>',start+1);
    if(end<0){*error="Incomplete ADIF field header";return {};}
    auto tag=text.mid(start,end-start+1);
    auto parts=text.mid(start+1,end-start-1).split(':');
    auto name=parts.first().trimmed().toUpper();
    pos=end+1;
    if(name=="EOH" || name=="EOR"){normalized+=tag;continue;}
    bool ok=false;auto length=parts.size()>=2?parts.at(1).toInt(&ok):0;
    if(!ok || length<0 || parts.size()>3){*error="Invalid ADIF field: "+name;return {};}
    if(length>text.size()-pos || splitsAdifTag(text,pos,pos+length)) {
      auto raw=text.mid(pos,qMin(length,text.size()-pos)).toUtf8().left(length);
      auto value=QString::fromUtf8(raw);
      if(raw.size()!=length || value.toUtf8()!=raw || value.size()==length ||
         splitsAdifTag(text,pos,pos+value.size())) {
        *error="Invalid ADIF value length: "+name;return {};
      }
      length=value.size();parts[1]=QString::number(length);
      tag='<'+parts.join(':')+'>';
    }
    normalized+=tag+text.mid(pos,length);pos+=length;
  }
  return normalized;
}
QList<OperatorLog::Record> readBytes(QString const& path,QByteArray const& bytes,QString* error,
                                    bool externalImport) {
  using namespace OperatorLog;
  QString localError;if(!error)error=&localError;error->clear();
  QTextStream in(bytes);in.setCodec("UTF-8");auto text=in.readAll();
  if(QFileInfo(path).suffix().compare("adi",Qt::CaseInsensitive)==0 || QFileInfo(path).suffix().compare("adif",Qt::CaseInsensitive)==0 || text.contains(QRegularExpression("<EOR>",QRegularExpression::CaseInsensitiveOption))) {
    if(externalImport)text=normalizeImportedAdif(text,error);
    return error->isEmpty()?OperatorLog::parse(text,error):QList<Record>{};
  }
  // Older JTDX also wrote a comma-separated wsjtx.log companion file.
  if(QFileInfo(path).suffix().compare("log",Qt::CaseInsensitive)==0) {
    QList<Record> records;
    for(auto line:text.split('\n')) {
      if(line.trimmed().isEmpty()) continue;
      auto fields=line.trimmed().split(',');
      if(fields.size()<9) {if(error) *error="Unsupported or incomplete legacy log row";return {};}
      auto date=QDate::fromString(fields[0],"yyyy-MM-dd");
      if(!date.isValid()) {if(error) *error="Invalid legacy log date";return {};}
      Record r{{"QSO_DATE",date.toString("yyyyMMdd")},{"TIME_ON",QString(fields[1]).remove(':')},{"CALL",fields[4].toUpper()},
               {"GRIDSQUARE",fields[5].toUpper()},{"FREQ",fields[6]},{"MODE",fields[7].toUpper()},{"RST_SENT",fields[8]}};
      if(fields.size()>9) r.insert("RST_RCVD",fields[9]);
      if(fields.size()>10) r.insert("TX_PWR",fields[10]);
      if(fields.size()>12) {
        r.insert("COMMENT",fields.mid(11,fields.size()-12).join(','));
        r.insert("NAME",fields.last());
      } else if(fields.size()>11) r.insert("COMMENT",fields[11]);
      auto offDate=QDate::fromString(fields[2],"yyyy-MM-dd");
      if(offDate.isValid()) r.insert("QSO_DATE_OFF",offDate.toString("yyyyMMdd"));
      r.insert("TIME_OFF",QString(fields[3]).remove(':'));
      records.append(r);
    }
    return records;
  }
  if(error) *error="No complete ADIF records found";
  return {};
}
}
QString OperatorLog::serialize(Record const& record) {
  QString out;for(auto i=record.begin();i!=record.end();++i) out+='<'+i.key()+':'+QString::number(i.value().size())+'>'+i.value();
  return out+"<EOR>\n";
}
QString OperatorLog::identity(Record const& r,QString const& fallback) {
  QString mode=keyField(r,"MODE");if(mode=="MFSK") mode=keyField(r,"SUBMODE");
  QString station=keyField(r,"STATION_CALLSIGN");if(station.isEmpty()) station=StationProfiles::normalize(fallback);
  QString time=keyField(r,"TIME_ON").leftJustified(6,'0');
  QString band=keyField(r,"BAND");if(band.isEmpty()) band=ADIF::bandFromFrequency(r.value("FREQ").toDouble()).toUpper();
  if(band=="2200M")band="2190M";
  return station+'|'+keyField(r,"CALL")+'|'+keyField(r,"QSO_DATE")+'|'+time+'|'+mode+'|'+band;
}
bool OperatorLog::append(QString const& path,QString const& adif,QString *error) {
  if(error)error->clear();
  if(path.isEmpty()) {if(error) *error="Select a callsign before logging";return false;}
  QString parseError;auto records=parse(adif,&parseError);
  if(!parseError.isEmpty() || records.isEmpty()){if(error)*error="Invalid or empty ADIF record: "+parseError;return false;}
  QDir dir=QFileInfo(path).dir();if(!dir.mkpath(".")) {if(error) *error="Cannot create log directory";return false;}
  QLockFile lock(dir.filePath(".operator-log.lock"));
  if(!lock.tryLock(3000)) {if(error) *error="Log is busy; retry after import finishes";return false;}
  auto call=StationProfiles::normalize(records.first().value("STATION_CALLSIGN"));
  QString recoveryError;
  if(!recoverUpdate(dir,call,&recoveryError)){if(error)*error=recoveryError;return false;}
  auto fullPath=dir.filePath("wsjtx_log.adi");
  if(isQuarterFile(path) && !QFile::exists(fullPath)) {
    if(call.isEmpty()){if(error)*error="Missing station callsign";return false;}
    auto base=dir;base.cdUp();auto result=synchronizeRecords(call,base);
    if(!result.errors.isEmpty()){if(error)*error=result.errors.join("; ");return false;}
  }
  QFile primary(path),full(fullPath);QList<QFile*> targets{&primary};
  if(isQuarterFile(path))targets.append(&full);
  QList<qint64> sizes;
  // Open both destinations before appending either; on a write/flush failure
  // roll back both lengths. A crash between writes is repaired by synchronize.
  for(auto file:targets) {
    if(!file->open(QIODevice::ReadWrite|QIODevice::Append)){if(error)*error=file->errorString();return false;}
    sizes.append(file->size());
  }
  for(int i=0;i<targets.size();++i) {
    auto file=targets[i];auto bytes=((sizes[i]==0?header():QString())+adif).toUtf8();
    if(file->write(bytes)!=bytes.size() || !file->flush()) {
      if(error)*error=file->errorString();
      for(int j=0;j<targets.size();++j) {
        if(!targets[j]->resize(sizes[j]) || !targets[j]->flush())
          if(error)*error+="; rollback failed: "+targets[j]->fileName();
      }
      return false;
    }
  }
  if(records.size()==1)for(int i=0;i<targets.size();++i) {
    auto file=targets[i];QsoPatch cached;cached.path=file->fileName();
    cached.offset=sizes[i]+(sizes[i]==0?header().toUtf8().size():0);cached.total=file->size();
    cached.before=adif.toUtf8();cached.tail=true;cached.anchor=anchorAt(*file,cached.offset);
    rememberPatch(identity(records.first(),call),cached);
  }
  return true;
}
bool OperatorLog::updateQso(QString const& callsign,Record const& original,Record const& replacement,
                            QString* error,QDir base) {
  QString localError;if(!error)error=&localError;error->clear();
  auto call=StationProfiles::normalize(callsign);auto id=identity(original,call);
  StationProfile profile;profile.call=call;profile.grid="AA00";
  auto validation=StationProfiles::validate(profile);
  auto date=QDate::fromString(original.value("QSO_DATE"),"yyyyMMdd");
  auto start=original.value("TIME_ON");
  auto startTime=QTime::fromString(start,start.size()==4?"hhmm":"hhmmss");
  auto offDate=replacement.value("QSO_DATE_OFF");auto offTime=replacement.value("TIME_OFF");
  auto endDate=offDate.isEmpty()?date:QDate::fromString(offDate,"yyyyMMdd");
  auto endTime=QTime::fromString(offTime,offTime.size()==4?"hhmm":"hhmmss");
  if(!validation.isEmpty() || !date.isValid() || original.value("CALL").trimmed().isEmpty()
     || !startTime.isValid() || !endDate.isValid()
     || (!offTime.isEmpty() && (!endTime.isValid() || endDate<date || (endDate==date && endTime<startTime)))
     || (!offDate.isEmpty() && offTime.isEmpty())
     || id!=identity(replacement,call)
     || (!original.value("STATION_CALLSIGN").isEmpty() && StationProfiles::normalize(original.value("STATION_CALLSIGN"))!=call)
     || (!replacement.value("STATION_CALLSIGN").isEmpty() && StationProfiles::normalize(replacement.value("STATION_CALLSIGN"))!=call)) {
    *error=QObject::tr("QSO identity changed or timestamps are invalid; log unchanged");return false;
  }
  auto dir=directory(call,base);QLockFile lock(dir.filePath(".operator-log.lock"));
  if(!lock.tryLock(3000)){*error="Log is busy; retry after import finishes";return false;}
  if(!recoverUpdate(dir,call,error))return false;
  auto quarter=quarterFile(call,date,base);
  if(!QFile::exists(quarter)) {
    for(auto const& path:files(call,base))
      if(QFileInfo(path).fileName().compare(QFileInfo(quarter).fileName(),Qt::CaseInsensitive)==0){quarter=path;break;}
  }
  QList<QsoPatch> patches;QList<Record> existing;
  for(auto const& path:{quarter,compatibilityFile(call,base)}) {
    if(QFileInfo(path).isSymLink()){*error=QObject::tr("Cannot revise a symbolic log link");return false;}
    QsoPatch patch;Record record;
    if(!locateQso(path,id,call,&patch,&record,error))return false;
    patches.append(patch);existing.append(record);
  }
  auto latest=existing.first();
  for(auto field=existing.last().cbegin();field!=existing.last().cend();++field)
    if(latest.value(field.key()).isEmpty()
       || (latest.value(field.key())==original.value(field.key()) && field.value()!=original.value(field.key())))
      latest[field.key()]=field.value();
  const QSet<QString> refreshed{"TIME_OFF","QSO_DATE_OFF","RST_SENT","RST_RCVD"};
  for(auto field=replacement.cbegin();field!=replacement.cend();++field)
    if(refreshed.contains(field.key()) || !original.contains(field.key()) || original.value(field.key())!=field.value())
      latest[field.key()]=field.value();
  latest["STATION_CALLSIGN"]=call;latest["QSO_DATE"]=original.value("QSO_DATE");latest["TIME_ON"]=original.value("TIME_ON");
  if(identity(latest,call)!=id){*error=QObject::tr("QSO identity changed or timestamps are invalid; log unchanged");return false;}
  auto after=serialize(latest).toUtf8();bool changed=false;QJsonArray entries;
  for(auto& patch:patches) {
    if(!currentPatch(patch,error))return false;
    patch.after=after;changed|=patch.before!=after;entries.append(patchJson(patch));
  }
  if(!changed)return true;
  QJsonObject transaction{{"version",1},{"station",call},{"patches",entries}};
  auto journal=QJsonDocument(transaction).toJson(QJsonDocument::Compact);
  if(journal.size()>8*1024*1024){*error=QObject::tr("QSO update exceeds the journal size limit");return false;}
  if(!save(updateJournal(dir),journal,error))return false;
  return recoverUpdate(dir,call,error);
}
OperatorLog::ImportResult OperatorLog::importFiles(QString active,QStringList paths,QDir base) {
  ImportResult result;active=StationProfiles::normalize(active);
  if(active.isEmpty())return result;
  StationProfile profile;profile.call=active;profile.grid="AA00";
  auto validation=StationProfiles::validate(profile);
  if(!validation.isEmpty()){result.errors.append(validation);return result;}
  auto dir=directory(active,base);
  paths.removeDuplicates();
  for(auto const& path:paths) {
    // Serialize imports with startup loads, log appends and callsign switching.
    QLockFile lock(dir.filePath(".operator-log.lock"));
    if(!lock.tryLock(10000)){result.errors.append(path+": log is busy; please retry");continue;}
    QString error;QByteArray originalBytes;
    if(!recoverUpdate(dir,active,&error)){result.errors.append(path+": "+error);continue;}
    if(!snapshot(path,&originalBytes,&error)){result.errors.append(path+": "+error);continue;}
    auto local=pathKey(QFileInfo(path).absolutePath())==pathKey(dir.absolutePath());
    auto maintained=local && (pathKey(QFileInfo(path).fileName())=="wsjtx_log.adi" || isQuarterFile(path));
    auto records=readBytes(path,originalBytes,&error,!maintained);
    if(!error.isEmpty() || (records.isEmpty() && !maintained)) {
      result.errors.append(path+": "+(error.isEmpty()?"Empty log":error));continue;
    }
    QMap<QString,QList<Record>> groups;int skipped=0;
    for(auto record:records) {
      auto station=StationProfiles::normalize(record.value("STATION_CALLSIGN"));
      if(!station.isEmpty() && station!=active){++skipped;continue;}
      auto date=QDate::fromString(record.value("QSO_DATE"),"yyyyMMdd");
      if(!date.isValid() || record.value("CALL").trimmed().isEmpty()) {
        error=invalidQsoError();break;
      }
      record["STATION_CALLSIGN"]=active;
      groups[quarterFile(active,date,base)].append(record);
    }
    if(!error.isEmpty()){result.errors.append(path+": "+error);continue;}
    if(maintained) {
      // The source is already a working input. The common synchronizer backs
      // up contaminated full/quarter files, filters and updates both formats.
      auto sync=synchronizeRecords(active,base);
      result.imported+=sync.imported;result.skipped+=sync.skipped;result.archived+=sync.archived;
      if(sync.errors.isEmpty())result.duplicates+=qMax(0,records.size()-skipped-sync.imported);
      result.errors.append(sync.errors);continue;
    }
    // Preserve the exact parsed bytes before touching any working log. External
    // drag sources remain where they were; deposited files move only on success.
    if(!archiveOriginal(dir,path,originalBytes,false,&error)){result.errors.append(path+": "+error);continue;}
    ++result.archived;result.skipped+=skipped;
    if(!unchanged(path,originalBytes,&error)){result.errors.append(path+": "+error);continue;}
    // Existing work files can also have been replaced with mixed logs by an
    // external logger. Sanitize them under the same lock before importing.
    auto before=synchronizeRecords(active,base);
    result.archived+=before.archived;result.skipped+=before.skipped;
    if(!before.errors.isEmpty()){result.errors.append(before.errors);continue;}
    for(auto group=groups.begin();group!=groups.end();++group) {
      QList<Record> existing;QByteArray bytes;bool existed=QFile::exists(group.key());
      if(existed) {
        if(!snapshot(group.key(),&bytes,&error))break;
        existing=readBytes(group.key(),bytes,&error);
      }
      if(!error.isEmpty())break;
      QMap<QString,Record> merged;
      for(auto const& r:existing) {auto id=identity(r,active);if(merged.contains(id))fillMissing(merged[id],r);else merged.insert(id,r);}
      int added=0;
      for(auto const& r:group.value()) {
        auto id=identity(r,active);
        if(merged.contains(id)){fillMissing(merged[id],r);++result.duplicates;}
        else {merged.insert(id,r);++added;}
      }
      if(existed && !unchanged(group.key(),bytes,&error))break;
      if(!existed && QFile::exists(group.key())){error=changedError();break;}
      if(!saveRecords(group.key(),merged.values(),&error))break;
      result.imported+=added;
    }
    if(!error.isEmpty()){result.errors.append(path+": "+error+"; original retained");continue;}
    auto synced=synchronizeRecords(active,base);
    result.archived+=synced.archived;result.skipped+=synced.skipped;
    result.errors.append(synced.errors);
    if(!synced.errors.isEmpty())continue;
    if(local) {
      if(!unchanged(path,originalBytes,&error) || !QFile::remove(path)) {
        result.errors.append(path+": converted, but original changed or could not be moved to old");continue;
      }
    }
  }
  return result;
}
