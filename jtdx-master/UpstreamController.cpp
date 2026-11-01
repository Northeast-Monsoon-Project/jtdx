// SPDX-License-Identifier: GPL-3.0-or-later
#include "UpstreamController.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QRegularExpression>
#include <cmath>
#include "moc_UpstreamController.cpp"

namespace {
constexpr int maxReceiveBytes=1800*12000*2;
constexpr int maxTransmitBytes=1800*48000*2;
constexpr char backendCommit[]="ccdfaf3c1c109010d15399674ce278167cfde848";
bool finite(QJsonObject const& object,char const* field) {
  return object.value(field).isDouble() && std::isfinite(object.value(field).toDouble());
}
bool integer(QJsonObject const& object,char const* field,double low,double high) {
  if(!finite(object,field))return false;
  const double value=object.value(field).toDouble();
  return value>=low && value<=high && value==std::floor(value);
}
bool messageValid(QJsonObject const& object) {
  const auto value=object.value("message");
  return value.isString() && !value.toString().trimmed().isEmpty() && value.toString().size()<=37 &&
    !value.toString().contains(QRegularExpression("[^ -~]"));
}
}
UpstreamController::UpstreamController(QObject *parent,QString program):QObject(parent),program_(program) {
  if(program_.isEmpty()) {
    program_=QDir(QCoreApplication::applicationDirPath()).filePath("jtdx-f-upstream");
#ifdef Q_OS_WIN
    program_+=".exe";
#endif
  }
  timeout_.setSingleShot(true);
  connect(&timeout_,&QTimer::timeout,this,[this]{fail(tr("The WSJT-X backend timed out. Reception can be restarted by selecting the mode again."));});
  connect(&process_,&QProcess::started,this,&UpstreamController::pump);
  connect(&process_,&QProcess::readyReadStandardOutput,this,&UpstreamController::receive);
  connect(&process_,&QProcess::readyReadStandardError,this,[this]{process_.readAllStandardError();});
  connect(&process_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){
    if(stopping_ && error==QProcess::FailedToStart) {
      // A cancelled Starting process may fail before it exists; Qt does not
      // emit finished() in this case. Unblock work queued for the next context.
      stopping_=false;clearFiles();clearBackendFiles();
      QTimer::singleShot(0,this,&UpstreamController::pump);
    } else if(!stopping_)fail(process_.errorString());
  });
  connect(&process_,static_cast<void(QProcess::*)(int,QProcess::ExitStatus)>(&QProcess::finished),this,[this](int,QProcess::ExitStatus){
    if(stopping_){stopping_=false;clearFiles();clearBackendFiles();pump();}
    else fail(tr("The WSJT-X backend stopped unexpectedly."));
  });
}
UpstreamController::~UpstreamController(){stop();process_.waitForFinished(1000);}
void UpstreamController::clearFiles(){if(!inputPath_.isEmpty())QFile::remove(inputPath_);if(!outputPath_.isEmpty())QFile::remove(outputPath_);inputPath_.clear();outputPath_.clear();}
void UpstreamController::clearBackendFiles(){if(directory_.isValid())QDir(directory_.filePath("backend")).removeRecursively();}
void UpstreamController::stop(){
  timeout_.stop();queue_.clear();busy_=false;hello_=false;input_.clear();current_=Job{};++txRevision_;
  if(process_.state()!=QProcess::NotRunning){stopping_=true;process_.kill();}else {stopping_=false;clearFiles();clearBackendFiles();}
}
void UpstreamController::setContext(QString const& context){if(context_==context)return;stop();context_=context;}
bool UpstreamController::encoding() const {
  if(busy_ && current_.request.value("op")=="encode" && current_.txRevision==txRevision_)return true;
  for(auto const& job:queue_)if(job.request.value("op")=="encode")return true;
  return false;
}
void UpstreamController::cancelTransmit(){++txRevision_;for(int i=queue_.size()-1;i>=0;--i)if(queue_[i].request.value("op")=="encode")queue_.removeAt(i);}
bool UpstreamController::enqueue(Job job){
  if(context_.isEmpty() || !directory_.isValid())return false;
  auto op=job.request.value("op").toString();
  for(int i=queue_.size()-1;i>=0;--i)if(queue_[i].request.value("op")==op)queue_.removeAt(i);
  if(op=="encode")queue_.prepend(job);else queue_.append(job);
  pump();return true;
}
bool UpstreamController::decode(QByteArray pcm,QJsonObject parameters){
  if(pcm.isEmpty() || pcm.size()%2 || pcm.size()>maxReceiveBytes)return false;
  parameters["op"]="decode";parameters["sample_count"]=pcm.size()/2;Job job;job.request=parameters;job.pcm=std::move(pcm);return enqueue(std::move(job));
}
bool UpstreamController::encode(QString message,QJsonObject parameters){
  if(encoding() || message.trimmed().isEmpty())return false;
  parameters["op"]="encode";parameters["message"]=message;Job job;job.request=parameters;job.txRevision=txRevision_;return enqueue(job);
}
void UpstreamController::astronomy(QJsonObject parameters){parameters["op"]="astro";Job job;job.request=parameters;enqueue(job);}
void UpstreamController::pump(){
  if(stopping_ || busy_ || queue_.isEmpty())return;
  if(process_.state()==QProcess::NotRunning){
    const auto workDirectory=directory_.filePath("backend");
    if(!QDir().mkpath(workDirectory)){fail(tr("Cannot prepare audio for the WSJT-X backend."));return;}
    hello_=false;process_.start(program_,{"--work-dir",workDirectory});timeout_.start(10000);return;
  }
  if(process_.state()!=QProcess::Running)return;
  if(!hello_){current_=Job{};current_.request={{"op","hello"}};}
  else current_=queue_.takeFirst();
  auto& request=current_.request;
  request["api"]=1;request["id"]=QString::number(++serial_);request["context"]=context_;
  auto op=request.value("op").toString();
  if(op=="decode") {
    inputPath_=directory_.filePath(QString::number(serial_)+"-in.pcm");QFile file(inputPath_);
    if(!file.open(QIODevice::WriteOnly) || file.write(current_.pcm)!=current_.pcm.size()){fail(tr("Cannot prepare audio for the WSJT-X backend."));return;}
    file.close();current_.pcm.clear();request["path"]=inputPath_;
  } else if(op=="encode") {
    outputPath_=directory_.filePath(QString::number(serial_)+"-out.pcm");request["path"]=outputPath_;
  }
  busy_=true;
  auto bytes=QJsonDocument(request).toJson(QJsonDocument::Compact)+'\n';
  if(process_.write(bytes)!=bytes.size()){fail(tr("Cannot communicate with the WSJT-X backend."));return;}
  timeout_.start(op=="decode"?qBound(30000,request.value("period").toInt(60)*1000,180000):30000);
}
void UpstreamController::receive(){
  // A killed process can still have buffered stdout. None of those bytes may
  // fail or complete work belonging to the next receive context.
  if(stopping_){process_.readAllStandardOutput();return;}
  input_+=process_.readAllStandardOutput();
  if(input_.size()>2*1024*1024){fail(tr("Invalid response from the WSJT-X backend."));return;}
  while(input_.contains('\n')) {
    int n=input_.indexOf('\n');auto line=input_.left(n);input_.remove(0,n+1);
    QJsonParseError error;auto document=QJsonDocument::fromJson(line,&error);
    if(error.error!=QJsonParseError::NoError || !document.isObject()){fail(tr("Invalid response from the WSJT-X backend."));return;}
    auto result=document.object();
    if(stopping_)continue;
    if(!busy_ || result.value("api")!=QJsonValue(1) || result.value("id")!=current_.request.value("id") || result.value("context")!=QJsonValue(context_)){fail(tr("Unexpected response from the WSJT-X backend."));return;}
    QString type=result.value("type").toString(),op=current_.request.value("op").toString();
    if(type=="error" || result.contains("error")){auto message=result.value("message").toString(result.value("error").toString());fail(message.isEmpty()?tr("The WSJT-X backend rejected the request."):message);return;}
    if(op=="hello" && type=="hello") {
      const auto modes=result.value("modes").toArray();
      if(result.value("version")!="3.0.2" || result.value("commit")!=backendCommit ||
         result.value("managed_work_dir")!=QJsonValue(true) ||
         !modes.contains("Q65") || !modes.contains("Echo") || !modes.contains("MSK144")) {
        fail(tr("Invalid response from the WSJT-X backend."));return;
      }
      hello_=true;complete();emit ready(result);
    }
    else if(op=="decode" && type=="decode") {
      if(result.value("mode")!=current_.request.value("mode") || !messageValid(result) ||
         !finite(result,"snr") || !finite(result,"dt") || !finite(result,"frequency")) {
        fail(tr("Invalid response from the WSJT-X backend."));return;
      }
      const auto request=current_.request;emit decoded(result,request);
    }
    else if(op=="decode" && type=="echo") {
      if(current_.request.value("mode")!="Echo" || !finite(result,"level_db") || !finite(result,"snr_db") ||
         !finite(result,"error_db") || !finite(result,"offset_hz") || !finite(result,"width_hz")) {
        fail(tr("Invalid response from the WSJT-X backend."));return;
      }
      const auto request=current_.request;emit echoData(result,request);
    }
    else if(op=="decode" && type=="done") {const auto request=current_.request;complete();emit decodeFinished(request);}
    else if(op=="astro" && type=="astro") {
      for(auto field:{"utc_ms","moon_az_deg","moon_el_deg","dx_moon_az_deg","dx_moon_el_deg", "sun_az_deg","sun_el_deg",
          "self_doppler_hz","dx_doppler_hz","echo_delay_seconds","width_self_hz","width_dx_hz", "sky_temperature_k","degradation_db","polarization_deg"}) {
        if(!finite(result,field)){fail(tr("Invalid response from the WSJT-X backend."));return;}
      }
      if(result.value("utc_ms")!=current_.request.value("utc_ms") ||
         (current_.request.contains("tx_utc_ms") && (result.value("tx_utc_ms")!=current_.request.value("tx_utc_ms") ||
          !finite(result,"tx_self_doppler_hz") || !finite(result,"tx_dx_doppler_hz")))) {
        fail(tr("Unexpected response from the WSJT-X backend."));return;
      }
      complete();emit astronomicalData(result);
    }
    else if(op=="encode" && type=="encoded") {
      if(current_.txRevision==txRevision_) {
        if(!integer(result,"sample_count",1,maxTransmitBytes/2) || result.value("sample_rate")!=QJsonValue(48000) ||
           !integer(result,"start_delay_ms",0,5000) || result.value("mode")!=current_.request.value("mode") ||
           result.value("path")!=QJsonValue(outputPath_) || !messageValid(result)) {
          fail(tr("Invalid transmit audio from the WSJT-X backend."));return;
        }
        QFile file(outputPath_);const qint64 count=qint64(result.value("sample_count").toDouble());
        if(!file.open(QIODevice::ReadOnly) || file.size()!=count*2){fail(tr("Invalid transmit audio from the WSJT-X backend."));return;}
        auto pcm=file.readAll();if(pcm.size()!=count*2){fail(tr("Cannot read the prepared transmit audio."));return;}
        file.close();complete();emit encoded(result.value("message").toString(),pcm,result);
      } else {
        complete();
      }
    } else {fail(tr("Unexpected response from the WSJT-X backend."));return;}
  }
}
void UpstreamController::complete(){timeout_.stop();busy_=false;clearFiles();current_=Job{};QTimer::singleShot(0,this,&UpstreamController::pump);}
void UpstreamController::fail(QString const& message){stop();emit error(message);}
