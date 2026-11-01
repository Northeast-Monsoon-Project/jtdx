// Restart-only updater. This executable must NOT link against Hamlib.
#include "HamlibUpdateSupport.hpp"
#include <hamlib/rig.h>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLibrary>
#include <QLockFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
QJsonObject failure(QString const& error){return {{"ok",false},{"error",error}};}
QJsonObject probe(QString const& candidate) {
  QString error;
  if(!HamlibUpdateSupport::validatePe(candidate,&error))return failure(error);
  QLibrary lib(candidate);lib.setLoadHints(QLibrary::ResolveAllSymbolsHint);
  if(!lib.load())return failure(lib.errorString());
  char const* required[]={"hamlib_version2","rig_cleanup","rig_close","rig_get_caps_cptr","rig_get_caps_int",
    "rig_get_conf2","rig_get_freq","rig_get_function_ptr","rig_get_level","rig_get_mode","rig_get_ptt",
    "rig_get_split_mode","rig_get_split_vfo","rig_get_vfo","rig_init","rig_list_foreach_model",
    "rig_load_all_backends","rig_open","rig_power2mW","rig_set_conf","rig_set_debug","rig_set_debug_callback",
    "rig_set_freq","rig_set_mode","rig_set_ptt","rig_set_split_freq","rig_set_split_freq_mode",
    "rig_set_split_mode","rig_set_split_vfo","rig_set_vfo","rig_token_lookup","rig_unregister","rig_vfo_op",
    "rig_data_pointer","rig_get_cache_timeout_ms","rig_set_cache_timeout_ms","rigerror"};
  for(auto name:required) if(!lib.resolve(name))return failure(QStringLiteral("Missing API: %1").arg(QString::fromLatin1(name)));
  auto versionPointer=reinterpret_cast<char const* const*>(lib.resolve("hamlib_version2"));
  QString version=QString::fromLatin1(*versionPointer);
  if(!QRegularExpression("^Hamlib 4\\.7\\.[0-9]+").match(version).hasMatch() || !version.contains("64-bit"))
    return failure(QStringLiteral("Unsupported Hamlib version: ")+version);
#define API(name) auto name=reinterpret_cast<decltype(&::name)>(lib.resolve(#name))
  API(rig_set_debug); API(rig_init);API(rig_open);API(rig_close);API(rig_cleanup);
  API(rig_set_freq);API(rig_get_freq);API(rig_get_vfo);API(rig_set_vfo);
  API(rig_set_split_vfo);API(rig_get_split_vfo);API(rig_data_pointer);
  rig_set_debug(RIG_DEBUG_NONE);
  // Only the in-memory Dummy backend is ever opened. No physical device.
  RIG* rig=rig_init(RIG_MODEL_DUMMY);
  if(!rig)return failure(QStringLiteral("Dummy initialization failed"));
  auto stop=[&](QString const& why){rig_close(rig);rig_cleanup(rig);return failure(why);};
  if(rig_data_pointer(rig,RIG_PTRX_STATE)!=&rig->state ||
     rig_data_pointer(rig,RIG_PTRX_PTTPORT)!=&rig->state.pttport ||
     !(rig->state.vfo_list&RIG_VFO_A) || !(rig->state.vfo_list&RIG_VFO_B))
    return stop(QStringLiteral("Hamlib RIG structure ABI is incompatible"));
  if(rig_open(rig)!=RIG_OK)return stop(QStringLiteral("Dummy open failed"));
  freq_t frequency=0;vfo_t vfo=RIG_VFO_NONE;split_t split=RIG_SPLIT_OFF;
  if(rig_set_vfo(rig,RIG_VFO_A)!=RIG_OK || rig_get_vfo(rig,&vfo)!=RIG_OK || vfo!=RIG_VFO_A ||
     rig_set_freq(rig,RIG_VFO_A,14074000)!=RIG_OK || rig_get_freq(rig,RIG_VFO_A,&frequency)!=RIG_OK || frequency!=14074000 ||
     rig_set_split_vfo(rig,RIG_VFO_A,RIG_SPLIT_ON,RIG_VFO_B)!=RIG_OK ||
     rig_get_split_vfo(rig,RIG_VFO_A,&split,&vfo)!=RIG_OK || split!=RIG_SPLIT_ON || vfo!=RIG_VFO_B ||
     rig->state.tx_vfo!=RIG_VFO_B)
    return stop(QStringLiteral("Dummy VFO/frequency/split compatibility check failed"));
  rig_close(rig);rig_cleanup(rig);lib.unload();
  return {{"ok",true},{"version",version}};
#undef API
}
bool writeJson(QString const& path,QJsonObject const& object){
  QSaveFile f(path);auto data=QJsonDocument(object).toJson();
  return f.open(QIODevice::WriteOnly)&&f.write(data)==data.size()&&f.commit();
}
}
int main(int argc,char** argv){
  QCoreApplication app(argc,argv);auto args=app.arguments();
  if(args.size()!=3)return 2;
  if(args[1]=="--probe"){
    auto result=probe(QFileInfo(args[2]).absoluteFilePath());
    QTextStream(stdout)<<QJsonDocument(result).toJson(QJsonDocument::Compact)<<Qt::endl;
    return result.value("ok").toBool()?0:1;
  }
  if(args[1]!="--apply")return 2;
#ifndef Q_OS_WIN
  return 2;
#else
  QDir directory(QCoreApplication::applicationDirPath());
  if(QFileInfo(args[2]).absoluteFilePath()!=directory.filePath(".hamlib-update-request.json"))return 2;
  QFile f(args[2]);if(!f.open(QIODevice::ReadOnly) || f.size()>65536)return 2;
  auto request=QJsonDocument::fromJson(f.readAll()).object();f.close();
  QString candidateName=request.value("candidate").toString();
  QString application=QFileInfo(request.value("application").toString()).absoluteFilePath();
  if((candidateName!="libhamlib-4_new.dll"&&candidateName!="libhamlib-4_backup.dll") ||
     QFileInfo(application).absolutePath()!=directory.absolutePath() ||
     QFileInfo(application).fileName().compare("jtdx-f.exe",Qt::CaseInsensitive)!=0)return 2;
  bool validPid=false;auto pid=request.value("pid").toString().toULong(&validPid);
  if(!validPid || !pid)return 2;
  // Capture the process handle before waiting, avoiding a reused PID later.
  HANDLE parent=OpenProcess(SYNCHRONIZE,FALSE,pid);
  if(parent){auto waited=WaitForSingleObject(parent,60000);CloseHandle(parent);if(waited!=WAIT_OBJECT_0){
    writeJson(directory.filePath(".hamlib-update-result.json"),failure("JTDX-F did not exit; no files were replaced"));return 1;
  }} else if(GetLastError()!=ERROR_INVALID_PARAMETER){
    writeJson(directory.filePath(".hamlib-update-result.json"),failure("Cannot wait for JTDX-F to exit"));return 1;
  }
  QLockFile lock(directory.filePath(".hamlib-update.lock"));
  if(!lock.tryLock(2000))return 1;
  auto active=directory.filePath("libhamlib-4.dll"),candidate=directory.filePath(candidateName),backup=directory.filePath("libhamlib-4_backup.dll");
  QJsonObject result;
  if(HamlibUpdateSupport::sha256(candidate)!=request.value("sha256").toString() ||
     HamlibUpdateSupport::sha256(active)!=request.value("activeSha256").toString())
    result=failure("Hamlib files changed after validation; no files were replaced");
  else {
    // Always use a separate probe process. Hamlib backends may hold module
    // references beyond QLibrary::unload; this installer never loads a DLL.
    QProcess checker;
    checker.setWorkingDirectory(directory.absolutePath());
    checker.start(QCoreApplication::applicationFilePath(),{"--probe",candidate});
    if(!checker.waitForStarted(5000) || !checker.waitForFinished(20000)) {
      checker.kill();checker.waitForFinished(1000);
      result=failure("Hamlib compatibility checker did not complete");
    } else {
      result=QJsonDocument::fromJson(checker.readAllStandardOutput()).object();
      if(checker.exitStatus()!=QProcess::NormalExit || checker.exitCode()!=0 || result.isEmpty())
        result=failure("Hamlib compatibility check failed");
    }
    if(result.value("ok").toBool() && HamlibUpdateSupport::sha256(candidate)!=request.value("sha256").toString())
      result=failure("Hamlib candidate changed during validation");
    if(result.value("ok").toBool()){
      QString error;
      if(!HamlibUpdateSupport::replaceWithBackup(candidate,active,backup,&error))result=failure(error);
      else {
        writeJson(directory.filePath(".hamlib-backup.json"),{{"version",request.value("oldVersion")},
                  {"sha256",HamlibUpdateSupport::sha256(backup)}});
        if(candidateName=="libhamlib-4_new.dll")QFile::remove(candidate);
      }
    }
  }
  writeJson(directory.filePath(".hamlib-update-result.json"),result);
  QFile::remove(args[2]);lock.unlock();
  QStringList restartArgs;for(auto const& value:request.value("arguments").toArray())restartArgs<<value.toString();
  // Startup is always disarmed by the normal JTDX-F TX safety gate.
  if(!QProcess::startDetached(application,restartArgs,directory.absolutePath()))return 1;
  return result.value("ok").toBool()?0:1;
#endif
}
