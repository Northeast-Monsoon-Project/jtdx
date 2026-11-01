// SPDX-License-Identifier: GPL-3.0-or-later
#include "JttyController.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include "moc_JttyController.cpp"
JttyController::JttyController(QObject* parent):QObject(parent) {
  deadline_.setSingleShot(true);deadline_.setInterval(30000);
  connect(&deadline_,&QTimer::timeout,this,[this]{fail(tr("JTTY decoder timed out. Select JTTY again to restart it."));});
  connect(&process_,&QProcess::started,this,&JttyController::pump);
  connect(&process_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){if(active_)fail(process_.errorString());});
  connect(&process_,static_cast<void(QProcess::*)(int,QProcess::ExitStatus)>(&QProcess::finished),this,[this](int,QProcess::ExitStatus){if(active_)fail(tr("JTTY decoder stopped unexpectedly."));});
  connect(&process_,&QProcess::readyReadStandardError,this,[this]{process_.readAllStandardError();});
  connect(&process_,&QProcess::readyReadStandardOutput,this,[this]{
    input_+=process_.readAllStandardOutput();
    if(input_.size()>8*1024*1024){fail(tr("Invalid JTTY decoder response."));return;}
    while(input_.contains('\n')) {
      auto length=input_.indexOf('\n');auto bytes=input_.left(length);input_.remove(0,length+1);
      QJsonParseError parse;auto document=QJsonDocument::fromJson(bytes,&parse);
      if(parse.error!=QJsonParseError::NoError || !document.isObject()){fail(tr("Invalid JTTY decoder response."));return;}
      auto result=document.object();auto id=result.value("id").toString().toULongLong();
      if(!busy_ || id!=inflight_){fail(tr("Unexpected JTTY decoder response."));return;}
      busy_=false;deadline_.stop();
      if(!result.value("error").toString().isEmpty()) {
        if(result.value("op")=="encode"){encodePending_=false;emit error(result.value("error").toString());}
        else {fail(result.value("error").toString());return;}
      } else if(result.value("op")=="encode") {
        if(encodePending_ && id==encodeId_ && !reset_) {
          encodePending_=false;
          auto pcm=QByteArray::fromBase64(result.value("pcm").toString().toLatin1());
          if(result.value("rate").toInt()!=48000 || pcm.isEmpty() || pcm.size()%2 || pcm.size()>48000*32*2){fail(tr("Invalid JTTY transmit audio."));return;}
          emit encoded(result.value("text").toString(),pcm);
        }
      } else if(!reset_)for(auto value:result.value("updates").toArray())emit received(value.toObject());
      pump();
    }
  });
}
JttyController::~JttyController(){setActive(false);process_.waitForFinished(1000);}
void JttyController::setActive(bool enabled) {
  if(!enabled){active_=false;deadline_.stop();process_.kill();busy_=false;input_.clear();audio_.clear();cancelTransmit();return;}
  if(!active_ && process_.state()!=QProcess::NotRunning)process_.waitForFinished(1000);
  active_=true;reset_=true;audio_.clear();cancelTransmit();
  if(process_.state()==QProcess::NotRunning) {
    QString program=QDir(QCoreApplication::applicationDirPath()).filePath("jtdx-f-jtty");
#ifdef Q_OS_WIN
    program+=".exe";
#endif
    process_.start(program,QStringList{});deadline_.start();
  } else pump();
}
void JttyController::reset(){audio_.clear();reset_=true;cancelTransmit();pump();}
void JttyController::cancelTransmit(){encode_=QJsonObject{};encodePending_=false;encodeId_=0;}
void JttyController::setReceiveRange(int low,int high,int center){low_=qBound(200,low,4800);high_=qBound(low_+1,high,5000);center_=qBound(200,center,4800);}
void JttyController::audio(QByteArray const& pcm) {
  if(!active_)return;
  if(pcm.size()%2 || audio_.size()+pcm.size()>12000*10*2){audio_.clear();reset_=true;emit error(tr("JTTY audio backlog exceeded 10 seconds; reception restarted."));}
  else audio_+=pcm;
  pump();
}
bool JttyController::encode(QString const& text,int frequency) {
  if(!active_ || encodePending_)return false;
  encodePending_=true;encode_={{"op","encode"},{"text",text},{"frequency",frequency},{"rate",48000}};pump();return true;
}
void JttyController::write(QJsonObject request) {
  inflight_=++serial_;request["id"]=QString::number(inflight_);busy_=true;
  if(request.value("op")=="encode")encodeId_=inflight_;
  auto bytes=QJsonDocument(request).toJson(QJsonDocument::Compact)+'\n';
  if(process_.write(bytes)!=bytes.size()){fail(tr("Cannot write to the JTTY decoder."));return;}
  deadline_.start();
}
void JttyController::pump() {
  if(!active_ || busy_ || process_.state()!=QProcess::Running)return;
  if(reset_){reset_=false;write({{"op","reset"}});}
  else if(!encode_.isEmpty()){auto request=encode_;encode_=QJsonObject{};write(request);}
  else if(!audio_.isEmpty()) {
    auto pcm=audio_.left(24000);audio_.remove(0,pcm.size());
    write({{"op","audio"},{"pcm",QString::fromLatin1(pcm.toBase64())},{"low",low_},{"high",high_},{"center",center_}});
  }
}
void JttyController::fail(QString const& message){setActive(false);emit error(message);}
