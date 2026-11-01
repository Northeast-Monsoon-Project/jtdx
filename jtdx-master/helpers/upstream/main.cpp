// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#include <io.h>
#define dup _dup
#define dup2 _dup2
#else
#include <unistd.h>
#endif
extern "C" {
void fm_upstream_init(char const*);
void fm_upstream_close();
void fm_upstream_q65(short const*,int,int,int,int,int,int,int,int,int,int,int,int,int,float,char const*,char const*,char const*,int*);
void fm_upstream_q65_encode(char const*,int*,char*);
void fm_upstream_msk_encode(char const*,int*,char*,int*);
void fm_upstream_msk(short const*,int,float,int,int,int,char const*,char const*,int);
void fm_upstream_astro(int,int,int,double,double,char const*,char const*,char const*,double*,int*);
void fm_upstream_echo(short*,int,int,int,int,int,float,float,float*,int*);
}
namespace {
QFile output;
QJsonObject current;
QString context;
QString ephemerisSource;
bool bound=false;
bool managedWorkDirectory=false;
constexpr int api=1;
constexpr char revision[]="ccdfaf3c1c109010d15399674ce278167cfde848";
void emitResult(QJsonObject value) {
  value["api"]=api;value["id"]=current.value("id");value["context"]=current.value("context");
  const auto bytes=QJsonDocument(value).toJson(QJsonDocument::Compact)+'\n';
  if(output.write(bytes)!=bytes.size() || !output.flush())std::exit(3);
}
void fail(char const* reason) {throw std::runtime_error(reason);}
int integer(QString const& key,int fallback,int low,int high) {
  const auto value=current.value(key);
  if(value.isUndefined())return fallback;
  const double number=value.toDouble(NAN);
  if(!std::isfinite(number) || number!=std::floor(number) || number<low || number>high)fail("invalid_integer");
  return int(number);
}
double number(QString const& key,double fallback,double low,double high) {
  const auto value=current.value(key);
  if(value.isUndefined()){if(!std::isfinite(fallback))fail("missing_number");return fallback;}
  const double n=value.toDouble(NAN);
  if(!std::isfinite(n) || n<low || n>high)fail("invalid_number");
  return n;
}
bool flag(QString const& key,bool fallback=false) {
  if(!current.contains(key))return fallback;
  if(!current.value(key).isBool())fail("invalid_boolean");
  return current.value(key).toBool();
}
QByteArray ascii(QString const& key,int max,QString fallback={}) {
  const auto value=current.value(key);
  if(!value.isUndefined() && !value.isString())fail("invalid_text");
  const QString text=value.isUndefined()?fallback:value.toString();
  if(text.size()>max || text.contains(QRegularExpression("[^ -~]")))fail("invalid_text");
  return text.toLatin1();
}
int period() {
  const int p=integer("period",60,15,300);
  if(p!=15 && p!=30 && p!=60 && p!=120 && p!=300)fail("invalid_period");
  return p;
}
int q65Nsps(int p) {
  switch(p){case 15:return 1800;case 30:return 3600;case 60:return 7200;case 120:return 16000;default:return 41472;}
}
int q65Submode(int p) {
  // Official WSJT-X 3.0.2 combinations; higher short-period tone spacings
  // exceed the supported receive bandwidth even before selecting TX audio.
  const int maximum=p==15?2:p==30?3:p==120?5:4;
  return integer("submode",0,0,maximum);
}
std::vector<short> loadPcm(int expected,int maximum) {
  const int count=integer("sample_count",expected,1,maximum);
  if(count>expected)fail("invalid_sample_count");
  const QString path=current.value("path").toString();
  QFile file(path);
  if(path.isEmpty() || !QFileInfo(file).isFile() || file.size()!=qint64(count)*2 || !file.open(QIODevice::ReadOnly))fail("invalid_pcm_file");
  const auto bytes=file.readAll();
  if(bytes.size()!=count*2)fail("pcm_read_failed");
  std::vector<short> pcm(maximum,0);
  for(int i=0;i<count;++i)pcm[i]=qFromLittleEndian<qint16>(reinterpret_cast<const uchar*>(bytes.constData())+2*i);
  return pcm;
}
void savePcm(std::vector<short> const& pcm) {
  const auto path=current.value("path").toString();
  if(path.isEmpty() || QFileInfo(path).isDir())fail("invalid_output_path");
  QByteArray bytes(int(pcm.size()*2),Qt::Uninitialized);
  for(size_t i=0;i<pcm.size();++i)qToLittleEndian<qint16>(pcm[i],reinterpret_cast<uchar*>(bytes.data())+2*i);
  QSaveFile file(path);
  if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit())fail("pcm_write_failed");
}
void encode() {
  const auto mode=current.value("mode").toString();
  const double frequency=number("audio_hz",1500,100,5000),rate=48000,pi=std::acos(-1.0);
  std::vector<short> pcm;
  char sent[38]={};int delay=0;
  if(mode=="Q65") {
    const int p=period(),sub=q65Submode(p),nsps=q65Nsps(p);
    const auto message=ascii("message",37);
    if(message.trimmed().isEmpty() || message.startsWith('@'))fail("invalid_message");
    const double spacing=12000.0/nsps*(1<<sub);
    if(frequency+65*spacing>=6000)fail("audio_frequency_out_of_band");
    int tones[85]={};fm_upstream_q65_encode(message.constData(),tones,sent);
    if(!sent[0] || QByteArray(sent).trimmed().isEmpty())fail("message_not_encodable");
    pcm.resize(85*nsps*4);double phase=0;
    for(size_t i=0;i<pcm.size();++i) {
      const int tone=tones[i/(nsps*4)];
      if(tone<0 || tone>65)fail("invalid_encoded_tone");
      phase=std::fmod(phase+2*pi*(frequency+tone*spacing)/rate,2*pi);
      const double envelope=std::min(1.0,std::min(double(i+1),double(pcm.size()-i))/960.0);
      pcm[i]=short(std::lround(30000*envelope*std::sin(phase)));
    }
    delay=p<=30?500:1000;
  } else if(mode=="MSK144") {
    const int p=integer("period",15,5,30);
    if(p!=5 && p!=10 && p!=15 && p!=30)fail("invalid_period");
    const auto message=ascii("message",37);
    if(message.trimmed().isEmpty() || message.startsWith('@'))fail("invalid_message");
    int tones[144]={},symbols=0;fm_upstream_msk_encode(message.constData(),tones,sent,&symbols);
    if(symbols!=144 && symbols!=40)fail("message_not_encodable");
    pcm.resize(p*48000-24000);double phase=0;
    for(size_t i=0;i<pcm.size();++i) {
      const int tone=tones[(i/24)%symbols];
      if(tone<0 || tone>1)fail("invalid_encoded_tone");
      phase=std::fmod(phase+2*pi*(1000+tone*1000)/rate,2*pi);
      const double envelope=std::min(1.0,std::min(double(i+1),double(pcm.size()-i))/816.0);
      pcm[i]=short(std::lround(30000*envelope*std::sin(phase)));
    }
  } else if(mode=="Echo") {
    pcm.resize(6*4096*4);double phase=0;
    for(size_t i=0;i<pcm.size();++i) {
      phase=std::fmod(phase+2*pi*frequency/rate,2*pi);
      const double envelope=std::min(1.0,std::min(double(i+1),double(pcm.size()-i))/960.0);
      pcm[i]=short(std::lround(30000*envelope*std::sin(phase)));
    }
    std::strcpy(sent,"ECHO");
  } else fail("unsupported_mode");
  savePcm(pcm);
  emitResult({{"type","encoded"},{"mode",mode},{"message",QString::fromLatin1(sent).trimmed()},
    {"path",current.value("path")},{"sample_rate",48000},{"sample_count",int(pcm.size())},
    {"start_delay_ms",delay},{"duration_ms",double(pcm.size())/48.0}});
}
void decode() {
  const auto mode=current.value("mode").toString();
  if(mode=="Q65") {
    const int p=period(),sub=q65Submode(p);
    auto pcm=loadPcm(p*12000,300*12000);
    const auto my=ascii("my_call",12),his=ascii("his_call",12),grid=ascii("his_grid",6);
    const int lo=integer("low_hz",200,100,4900),hi=integer("high_hz",4000,200,5000);
    if(lo>=hi)fail("invalid_frequency_range");
    int averages=0;
    fm_upstream_q65(pcm.data(),p,sub,integer("utc",0,0,235959),integer("rx_hz",1500,100,5000),
      integer("tolerance",100,10,1000),lo,hi,integer("depth",2,1,3),integer("max_drift",0,0,100),
      integer("progress",0,0,5),flag("average"),flag("clear_average"),flag("single_decode"),float(number("eme_delay",0,0,5.5)),
      my.constData(),his.constData(),grid.constData(),&averages);
    emitResult({{"type","done"},{"average_even",averages/1000},{"average_odd",averages%1000}});
  } else if(mode=="MSK144") {
    if(integer("sample_count",7168,1,7168)!=7168)fail("invalid_sample_count");
    auto pcm=loadPcm(7168,7168);
    const auto my=ascii("my_call",12),his=ascii("his_call",12);
    fm_upstream_msk(pcm.data(),integer("utc",0,0,235959),float(number("tsec",0,0,30)),
      integer("rx_hz",1500,500,2500),integer("tolerance",100,10,200),integer("depth",2,1,3),
      my.constData(),his.constData(),flag("short_messages"));
    emitResult({{"type","done"}});
  } else if(mode=="Echo") {
    auto pcm=loadPcm(7*4096,7*4096);float values[6]={};int counts[2]={};
    fm_upstream_echo(pcm.data(),integer("doppler_hz",0,-3000,3000),integer("rit_hz",0,-1000,1000),
      flag("auto_mode"),integer("averages",1,1,100),flag("clear_average"),float(number("audio_hz",1500,1000,2000)),
      float(number("width_hz",1,0.04,700)),values,counts);
    for(float v:values)if(!std::isfinite(v))fail("invalid_echo_result");
    emitResult({{"type","echo"},{"level_db",values[0]},{"snr_db",values[1]},{"error_db",values[2]},
      {"offset_hz",values[3]},{"width_hz",values[4]},{"quality",counts[0]},{"integrations",counts[1]}});
    emitResult({{"type","done"}});
  } else fail("unsupported_mode");
}
void astro() {
  const auto date=QDateTime::fromMSecsSinceEpoch(qint64(number("utc_ms",NAN,0,4102444800000.0)),Qt::UTC);
  if(!date.isValid())fail("invalid_utc");
  const auto my=ascii("my_grid",6),his=ascii("his_grid",6);
  const QRegularExpression grid("^[A-R]{2}[0-9]{2}([A-X]{2})?$");
  if(!grid.match(QString::fromLatin1(my).toUpper()).hasMatch() || (!his.isEmpty() && !grid.match(QString::fromLatin1(his).toUpper()).hasMatch()))fail("invalid_grid");
  const QString path=current.value("ephemeris_path").toString();
  QFileInfo file(path);
  if(!file.isFile() || !file.isReadable() || file.size()<1000000)fail("invalid_ephemeris_file");
  // Fortran's narrow-character OPEN cannot represent every Windows path.
  // Qt copies the Unicode source into our Unicode-aware working directory;
  // the Fortran code sees only this ASCII relative name. Its JPL cache is
  // process-private, so replacing the underlying ephemeris requires restart.
  const QString source=file.canonicalFilePath();
  if(ephemerisSource.isEmpty()) {
    if(!QFile::copy(path,QStringLiteral("JPLEPH")))fail("ephemeris_copy_failed");
    ephemerisSource=source;
  } else if(ephemerisSource!=source)fail("ephemeris_changed_restart_required");
  double values[20]={};int integers[3]={};
  fm_upstream_astro(date.date().year(),date.date().month(),date.date().day(),date.time().msecsSinceStartOfDay()/3600000.0,
    number("frequency_hz",144000000,100000,1000000000000.0),my.toUpper().constData(),his.toUpper().constData(),"JPLEPH",values,integers);
  for(double v:values)if(!std::isfinite(v))fail("invalid_astro_result");
  QJsonObject result{{"type","astro"},{"utc_ms",double(date.toMSecsSinceEpoch())},
    {"sun_az_deg",values[0]},{"sun_el_deg",values[1]},
    {"moon_az_deg",values[2]},{"moon_el_deg",values[3]},
    {"dx_moon_az_deg",values[4]},{"dx_moon_el_deg",values[5]},
    {"sky_temperature_k",integers[0]},{"dx_doppler_hz",integers[1]},{"self_doppler_hz",integers[2]},
    {"degradation_db",values[10]},{"polarization_deg",values[12]},
    {"width_self_hz",values[16]},{"width_dx_hz",values[17]},{"echo_delay_seconds",values[19]}};
  if(current.contains("tx_utc_ms")) {
    const auto tx=QDateTime::fromMSecsSinceEpoch(qint64(number("tx_utc_ms",NAN,0,4102444800000.0)),Qt::UTC);
    if(!tx.isValid())fail("invalid_utc");
    fm_upstream_astro(tx.date().year(),tx.date().month(),tx.date().day(),tx.time().msecsSinceStartOfDay()/3600000.0,
      number("frequency_hz",144000000,100000,1000000000000.0),my.toUpper().constData(),his.toUpper().constData(),"JPLEPH",values,integers);
    result["tx_utc_ms"]=double(tx.toMSecsSinceEpoch());
    result["tx_dx_doppler_hz"]=integers[1];result["tx_self_doppler_hz"]=integers[2];
  }
  emitResult(result);
}
}
extern "C" void fm_upstream_decoded(char const* message,int snr,float dt,float freq,int method,int averaged) {
  if(!std::isfinite(dt) || !std::isfinite(freq) || method<0)return;
  emitResult({{"type","decode"},{"mode",current.value("mode")},{"message",QString::fromLatin1(message).trimmed()},
    {"snr",snr},{"dt",dt},{"frequency",freq},{"method",method},{"averaged",averaged}});
}
int main(int argc,char** argv) {
  QCoreApplication app(argc,argv);
  // Preserve a separate protocol descriptor. Upstream Fortran/C diagnostic
  // writes to stdout are redirected to stderr and cannot corrupt JSON replies.
  const int protocol=dup(1);if(protocol<0 || dup2(2,1)<0 || !output.open(protocol,QIODevice::WriteOnly,QFileDevice::AutoCloseHandle))return 3;
  std::unique_ptr<QTemporaryDir> scratch;
  QString workingDirectory;
  const auto arguments=app.arguments();
  if(arguments.size()==3 && arguments[1]=="--work-dir") {
    workingDirectory=arguments[2];
    if(!QDir::isAbsolutePath(workingDirectory) || !QFileInfo(workingDirectory).isDir())return 3;
    managedWorkDirectory=true;
  } else if(arguments.size()==1) {
    scratch.reset(new QTemporaryDir);
    if(!scratch->isValid())return 3;
    workingDirectory=scratch->path();
  } else return 2;
  const QString previousDirectory=QDir::currentPath();
  if(!QDir::setCurrent(workingDirectory))return 3;
  struct RestoreDirectory {
    QString previous;
    ~RestoreDirectory(){fm_upstream_close();QDir::setCurrent(previous);}
  } restore{previousDirectory};
  fm_upstream_init(".");
  char input[65538];
  while(std::fgets(input,sizeof(input),stdin)) {
    const QByteArray line(input);
    current={};
    try {
      if(line.size()>65536 || !line.endsWith('\n')) {
        emitResult({{"type","error"},{"code","request_too_large_or_unterminated"}});return 2;
      }
      QJsonParseError error;const auto document=QJsonDocument::fromJson(line,&error);
      if(error.error!=QJsonParseError::NoError || !document.isObject())fail("invalid_json");
      current=document.object();
      if(integer("api",0,1,1)!=api || !current.value("id").isString() || !current.value("context").isString())fail("invalid_envelope");
      const auto op=current.value("op").toString();
      if(op=="hello") {
        emitResult({{"type","hello"},{"version","3.0.2"},{"commit",revision},
          {"managed_work_dir",managedWorkDirectory},
          {"modes",QJsonArray{"Q65","Echo","MSK144"}},{"q65_periods",QJsonArray{15,30,60,120,300}},
          {"msk144_periods",QJsonArray{5,10,15,30}},{"msk144_window_samples",7168},{"msk144_hop_samples",3584},
          {"q65_submodes",QJsonArray{"A","B","C","D","E","F"}},{"astro",true}});continue;
      }
      if(op=="reset") {emitResult({{"type","done"},{"reset",true},{"restart_required",true}});return 0;}
      if(!bound){context=current.value("context").toString();bound=true;}
      if(context!=current.value("context").toString())fail("context_mismatch");
      if(op=="encode")encode();else if(op=="decode")decode();else if(op=="astro")astro();else fail("unsupported_operation");
    } catch(std::exception const& error) {
      emitResult({{"type","error"},{"code",error.what()},{"message",error.what()}});
    }
  }
  return 0;
}
