#include "OperatorLogLoader.hpp"
#include "StationProfiles.hpp"
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QLockFile>
#include <QMutexLocker>
#include <exception>

namespace {
QByteArray fingerprint(QStringList const& paths,QString* error) {
  QCryptographicHash hash(QCryptographicHash::Sha256);
  for(auto const& path:paths) {
    auto name=path.toUtf8();
    hash.addData(QByteArray::number(name.size())+':'+name);
    QFile file(path);
    if(!file.exists()) {hash.addData("0",1);continue;}
    hash.addData("1",1);
    if(!file.open(QIODevice::ReadOnly)) {
      *error=path+": "+file.errorString();return {};
    }
    hash.addData(QByteArray::number(file.size())+':');
    if(!hash.addData(&file)) {*error=path+": "+file.errorString();return {};}
  }
  return hash.result();
}
QByteArray logFingerprint(QString const& call,QDir const& base,QString* error) {
  auto paths=OperatorLog::files(call,base);
  auto full=OperatorLog::compatibilityFile(call,base);
  if(!full.isEmpty())paths.append(full);
  return fingerprint(paths,error);
}
QByteArray databaseFingerprint(QDir const& base,QString* error) {
  // LoTW activity is filtered relative to today's date by CountryDat::load.
  return fingerprint(LogBook::databaseFiles(base),error)+QDate::currentDate().toString(Qt::ISODate).toLatin1();
}
}

void OperatorLogLoader::clear() {
  QMutexLocker guard(&mutex_);entries_.clear();
}

OperatorLogLoader::Result OperatorLogLoader::load(QString const& callsign,
    QString const& grid,QString const& date,QDir base) {
  QElapsedTimer timer;timer.start();
  QMutexLocker guard(&mutex_);
  Result result;result.call=StationProfiles::normalize(callsign);
  result.grid=grid;result.date=date;
  auto finish=[&] {result.elapsedMs=timer.elapsed();return result;};
  auto root=base.absolutePath();
  try {
    if(!base.mkpath(".")) {result.error=QObject::tr("Cannot create log directory");return finish();}
    // Empty callsign at first launch still needs the country database, but must
    // not create a compatibility log in the application's data root.
    QLockFile lock(OperatorLog::directory(result.call,base).filePath(".operator-log.lock"));
    if(!lock.tryLock(10000)) {result.error=QObject::tr("Log is busy. Please retry.");return finish();}
    // A journal may exist even when a crash preceded the first byte change.
    // Recover under the lock before an unchanged fingerprint can hit cache.
    if(!OperatorLog::detail::recoverPendingUpdateLocked(result.call,base,&result.error))return finish();
    auto logs=logFingerprint(result.call,base,&result.error);
    auto databases=databaseFingerprint(base,&result.error);
    if(!result.error.isEmpty())return finish();
    bool alreadySynchronized=false;
    for(int i=0;i<entries_.size();++i) {
      auto const& entry=entries_.at(i);
      if(entry.root!=root || entry.call!=result.call || entry.logs!=logs)continue;
      alreadySynchronized=true;
      if(entry.grid!=grid || entry.date!=date || entry.databases!=databases)continue;
      result.book=entry.book;result.cacheHit=true;
      entries_.move(i,0);
      return finish();
    }

    // Re-filtering an already synchronized log (grid/date/database change)
    // only rebuilds its indexes. New or changed log bytes require the union.
    if(!alreadySynchronized) {
      auto sync=OperatorLog::detail::synchronizeLocked(result.call,base);
      result.synchronized=true;
      if(!sync.errors.isEmpty()){result.error=sync.errors.join("; ");return finish();}
    }
    logs=logFingerprint(result.call,base,&result.error);
    databases=databaseFingerprint(base,&result.error);
    if(!result.error.isEmpty())return finish();
    result.book.init(result.call,grid,date,result.call,base);
    auto finalLogs=logFingerprint(result.call,base,&result.error);
    auto finalDatabases=databaseFingerprint(base,&result.error);
    if(!result.error.isEmpty())return finish();
    if(logs!=finalLogs || databases!=finalDatabases) {
      // External programs need not honor our lock. Never publish a snapshot
      // that changed while its indexes were being built; the watcher retries.
      result.error=QObject::tr("Log files changed while loading. Please retry.");
      return finish();
    }
    for(int i=entries_.size()-1;i>=0;--i)
      if(entries_[i].root==root && entries_[i].call==result.call &&
         (entries_[i].logs!=logs || entries_[i].databases!=databases ||
          (entries_[i].grid==grid && entries_[i].date==date)))entries_.removeAt(i);
    Entry entry;entry.root=root;entry.call=result.call;entry.grid=grid;entry.date=date;
    entry.logs=logs;entry.databases=databases;entry.book=result.book;entry.qsoCount=result.book.get_qso_count();
    entries_.prepend(entry);
    qint64 records=0;for(auto const& entry:entries_)records+=entry.qsoCount;
    while(entries_.size()>1 && (entries_.size()>4 || records>recordBudget_))
      records-=entries_.takeLast().qsoCount;
  } catch(std::exception const& error) {result.error=QString::fromLocal8Bit(error.what());}
  catch(...) {result.error=QObject::tr("Unable to read the log files.");}
  return finish();
}
