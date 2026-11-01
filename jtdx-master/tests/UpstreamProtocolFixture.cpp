// Synthetic child process for controller failure/race tests; no DSP or radio.
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <cstdio>
int main(int argc,char** argv) {
  QCoreApplication app(argc,argv);
  const auto arguments=app.arguments();
  const int workIndex=arguments.indexOf("--work-dir");
  const bool managedWorkDirectory=workIndex>=0 && workIndex+1<arguments.size() && QDir::setCurrent(arguments[workIndex+1]);
  const QString scenario=qEnvironmentVariable("FM_UPSTREAM_TEST_SCENARIO");
  char line[65538];
  while(std::fgets(line,sizeof line,stdin)) {
    auto request=QJsonDocument::fromJson(line).object();
    auto send=[&](QJsonObject result) {
      result["api"]=1;result["id"]=request["id"];result["context"]=request["context"];
      if(scenario=="bad_api")result["api"]=2;
      if(scenario=="bad_id")result["id"]="stale";
      const auto bytes=QJsonDocument(result).toJson(QJsonDocument::Compact)+'\n';
      std::fwrite(bytes.constData(),1,size_t(bytes.size()),stdout);std::fflush(stdout);
    };
    const auto op=request.value("op").toString();
    if(op=="hello") {
      if(scenario=="hang"){QThread::msleep(60000);return 0;}
      if(scenario=="malformed"){std::puts("{bad json");std::fflush(stdout);continue;}
      QJsonObject result{{"type","hello"},{"version","3.0.2"},
        {"commit","ccdfaf3c1c109010d15399674ce278167cfde848"},
        {"managed_work_dir",managedWorkDirectory},
        {"modes",QJsonArray{"Q65","Echo","MSK144"}}};
      if(scenario=="bad_version")result["version"]="2.7.0";
      if(scenario=="bad_commit")result["commit"]="unknown";
      if(scenario=="missing_mode")result["modes"]=QJsonArray{"Q65"};
      if(scenario=="unmanaged_work_dir")result["managed_work_dir"]=false;
      if(scenario=="missing_work_dir")result.remove("managed_work_dir");
      if(scenario=="parent_scratch") {
        QFile ephemeris("JPLEPH");
        if(!managedWorkDirectory || !ephemeris.open(QIODevice::WriteOnly))return 4;
        ephemeris.write(request.value("context").toString().toUtf8());ephemeris.close();
      }
      send(result);
    } else if(op=="encode") {
      if(scenario=="delay_encode")QThread::msleep(150);
      QFile file(request.value("path").toString());
      if(!file.open(QIODevice::WriteOnly))return 2;
      file.write(QByteArray::fromHex("010002000300"));file.close();
      QJsonObject result{{"type","encoded"},{"mode",request["mode"]},{"message",request["message"]},
        {"path",request["path"]},{"sample_rate",48000},{"sample_count",3},{"start_delay_ms",500}};
      if(scenario=="fractional_count")result["sample_count"]=3.5;
      if(scenario=="huge_count")result["sample_count"]=1.e100;
      if(scenario=="bad_mode")result["mode"]="FT8";
      if(scenario=="bad_delay")result["start_delay_ms"]=-500;
      if(scenario=="wrong_path")result["path"]="different.pcm";
      if(scenario=="wrong_size")result["sample_count"]=4;
      send(result);
    } else if(op=="decode") {
      send({{"type","decode"},{"mode",request["mode"]},{"message","CQ K1ABC FN20"},
        {"snr",-20},{"dt",0.1},{"frequency",1500}});
      send({{"type","done"}});
    } else return 3;
  }
}
