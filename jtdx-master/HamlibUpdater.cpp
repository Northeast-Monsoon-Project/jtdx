#include "HamlibUpdater.hpp"
#include "HamlibUpdateSupport.hpp"
#include <hamlib/rig.h>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>

HamlibUpdater::HamlibUpdater(QObject *parent):QObject(parent) {
  timeout_.setSingleShot(true);
  connect(&timeout_,&QTimer::timeout,this,[this]{
    if(reply_) reply_->abort();
    if(probe_.state()!=QProcess::NotRunning) probe_.kill();
    fail(tr("Hamlib update timed out. The current version is unchanged."));
  });
  connect(&probe_,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),this,
    [this](int code,QProcess::ExitStatus status){
      if(!busy_)return;
      timeout_.stop();
      auto result=QJsonDocument::fromJson(probe_.readAllStandardOutput()).object();
      if(code || status!=QProcess::NormalExit || !result.value("ok").toBool()) {
        fail(tr("Hamlib compatibility check failed: %1").arg(result.value("error").toString(probe_.errorString())));return;
      }
      readyVersion_=result.value("version").toString();
      readyHash_=HamlibUpdateSupport::sha256(candidate_);
      activeHash_=HamlibUpdateSupport::sha256(path("libhamlib-4.dll"));
      if(readyHash_.isEmpty() || activeHash_.isEmpty()){fail(tr("Cannot read the Hamlib files."));return;}
      busy_=false;
      emit statusChanged(tr("Compatibility check passed. Restart JTDX-F to apply %1.").arg(readyVersion_));
      emit readyToRestart(readyVersion_,rollback_);
    });
  connect(&probe_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){
    if(busy_ && error==QProcess::FailedToStart)fail(tr("Cannot start the Hamlib compatibility checker: %1").arg(probe_.errorString()));
  });
  QTimer::singleShot(0,this,[this]{
    QFile f(path(".hamlib-update-result.json"));
    if(f.open(QIODevice::ReadOnly)) {
      auto result=QJsonDocument::fromJson(f.readAll()).object(); f.close();
      if(!result.isEmpty()) emit statusChanged(result.value("ok").toBool()
        ? tr("Hamlib update completed successfully.")
        : tr("Hamlib update was not applied: %1").arg(result.value("error").toString()));
      QFile::remove(f.fileName());
    }
  });
}
HamlibUpdater::~HamlibUpdater(){
  timeout_.stop();
  if(reply_){reply_->disconnect(this);reply_->abort();}
  if(probe_.state()!=QProcess::NotRunning){probe_.disconnect(this);probe_.kill();probe_.waitForFinished(1000);}
}
QString HamlibUpdater::path(QString const& name)const{return QDir(QCoreApplication::applicationDirPath()).filePath(name);}
QString HamlibUpdater::currentVersion()const{return QString::fromLatin1(hamlib_version2);}
QString HamlibUpdater::backupVersion()const{
  QFile f(path(".hamlib-backup.json"));
  if(!f.open(QIODevice::ReadOnly))return {};
  auto o=QJsonDocument::fromJson(f.readAll()).object();
  if(o.value("sha256").toString()!=HamlibUpdateSupport::sha256(path("libhamlib-4_backup.dll")))return {};
  return o.value("version").toString();
}
QString HamlibUpdater::availabilityReason()const{
#ifndef Q_OS_WIN
  return tr("On this platform, Hamlib is updated with JTDX-F.");
#else
  if(sizeof(void*)!=8)return tr("Hamlib updates require a 64-bit Windows installation.");
  if(!QFileInfo::exists(path("jtdx-hamlib-update.exe")))return tr("The Hamlib update helper is missing. Reinstall JTDX-F.");
  if(!QFileInfo(QCoreApplication::applicationDirPath()).isWritable())return tr("The installation folder is not writable. Run JTDX-F with permission to update this folder.");
  return {};
#endif
}
bool HamlibUpdater::canUpdate()const{return availabilityReason().isEmpty();}
bool HamlibUpdater::begin(){
  if(busy_)return false;
  if(!canUpdate()){emit failed(availabilityReason());return false;}
  if(!lock_)lock_.reset(new QLockFile(path(".hamlib-update.lock")));
  if(!lock_->isLocked() && !lock_->tryLock(0)){emit failed(tr("Another Hamlib update is in progress."));return false;}
  readyVersion_.clear(); readyHash_.clear(); busy_=true;
  return true;
}
void HamlibUpdater::fail(QString const& message){
  timeout_.stop(); busy_=false; readyVersion_.clear(); readyHash_.clear();
  if(output_){output_->cancelWriting();output_.reset();}
  if(lock_)lock_->unlock();
  emit failed(message); emit statusChanged(message);
}
void HamlibUpdater::downloadUpdate(){
  if(!begin())return;
  candidate_=path("libhamlib-4_new.dll");rollback_=false;
  output_.reset(new QSaveFile(candidate_));output_->setDirectWriteFallback(false);
  if(!output_->open(QIODevice::WriteOnly)){fail(tr("Cannot stage Hamlib: %1").arg(output_->errorString()));return;}
  // Stay on ABI-compatible 4.7, never follow the 5.x development channel.
  QNetworkRequest req(QUrl("https://hamlib.sourceforge.net/snapshots-4.7/dll64/libhamlib-4.dll"));
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
  req.setRawHeader("User-Agent","JTDX-F Hamlib updater");
  reply_=network_.get(req); auto reply=reply_.data();
  connect(reply,&QNetworkReply::downloadProgress,this,&HamlibUpdater::progress);
  connect(reply,&QIODevice::readyRead,this,[this,reply]{
    if(!busy_ || !output_)return;
    auto data=reply->readAll();
    if(output_->size()+data.size()>32*1024*1024 || output_->write(data)!=data.size()){
      reply->abort(); fail(tr("The Hamlib download is too large or could not be saved."));
    }
  });
  connect(reply,&QNetworkReply::finished,this,[this,reply]{
    reply->deleteLater();
    if(!busy_ || !output_)return;
    timeout_.stop();
    if(reply->error()!=QNetworkReply::NoError){fail(tr("Hamlib download failed: %1").arg(reply->errorString()));return;}
    auto remaining=reply->readAll();
    if(output_->size()+remaining.size()>32*1024*1024 || output_->write(remaining)!=remaining.size() || !output_->commit()){
      fail(tr("Cannot save the downloaded Hamlib file."));return;
    }
    output_.reset();probe(candidate_,false);
  });
  timeout_.start(120000);
  emit statusChanged(tr("Downloading the official Hamlib 4.7 branch update…"));
}
void HamlibUpdater::rollback(){
  if(!begin())return;
  candidate_=path("libhamlib-4_backup.dll");rollback_=true;
  if(!QFileInfo::exists(candidate_)){fail(tr("No previous Hamlib version is available."));return;}
  probe(candidate_,true);
}
void HamlibUpdater::probe(QString const& candidate,bool rollback){
  QString error;
  if(!HamlibUpdateSupport::validatePe(candidate,&error)){fail(tr("Invalid Hamlib update: %1").arg(error));return;}
  candidate_=candidate;rollback_=rollback;
  probe_.setProgram(path("jtdx-hamlib-update.exe"));
  probe_.setArguments({"--probe",candidate});
  probe_.setWorkingDirectory(QCoreApplication::applicationDirPath());
  probe_.start();timeout_.start(20000);
  emit statusChanged(tr("Checking Hamlib compatibility without connecting to a radio…"));
}
void HamlibUpdater::restartApplication(){
  if(busy_ || readyVersion_.isEmpty())return;
  if(readyHash_!=HamlibUpdateSupport::sha256(candidate_) || activeHash_!=HamlibUpdateSupport::sha256(path("libhamlib-4.dll"))){
    fail(tr("Hamlib files changed after validation. Check the update again."));return;
  }
  QJsonObject request{{"pid",QString::number(QCoreApplication::applicationPid())},
    {"application",QCoreApplication::applicationFilePath()},
    {"arguments",QJsonArray::fromStringList(QCoreApplication::arguments().mid(1))},
    {"candidate",QFileInfo(candidate_).fileName()},{"sha256",readyHash_},{"activeSha256",activeHash_},
    {"oldVersion",currentVersion()},{"newVersion",readyVersion_}};
  QSaveFile f(path(".hamlib-update-request.json"));
  auto json=QJsonDocument(request).toJson();
  if(!f.open(QIODevice::WriteOnly)||f.write(json)!=json.size()||!f.commit()){
    fail(tr("Cannot save the Hamlib restart request: %1").arg(f.errorString()));return;
  }
  if(!QProcess::startDetached(path("jtdx-hamlib-update.exe"),{"--apply",f.fileName()},QCoreApplication::applicationDirPath())){
    fail(tr("Cannot start the Hamlib update helper."));return;
  }
  busy_=true;
  emit statusChanged(tr("Restarting JTDX-F to apply the Hamlib update…"));
  emit restartRequested();
}
#include "moc_HamlibUpdater.cpp"
