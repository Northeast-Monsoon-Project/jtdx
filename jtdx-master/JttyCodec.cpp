// JTDX-F JTTY codec subprocess. Serialized DSP, bounded audio and JSON lines.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "JttyDecoder.hpp"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QtEndian>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>
#ifdef Q_OS_WIN
#include <io.h>
#include <fcntl.h>
#endif
extern "C" {
void genjtty_text_c(char*,int,int,int*,int*,char*,int*,int*,int*);
void gen_jttywave_(int*,int*,int*,float*,float*,float*,float*,float*,int*,int*);
}
namespace {
void output(QJsonObject const& object) {
  auto bytes=QJsonDocument(object).toJson(QJsonDocument::Compact)+'\n';
  std::fwrite(bytes.constData(),1,bytes.size(),stdout);std::fflush(stdout);
}
QJsonObject encode(QJsonObject const& request) {
  auto text=request.value("text").toString().simplified().toUpper();
  if(text.isEmpty() || text.size()>80)return {{"error","JTTY text must contain 1 to 80 characters"}};
  // The encoder alphabet is explicitly limited; do not silently replace text.
  const QString alphabet="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ +-./?!\"#$%,&*()_'=[]{}<>|:;";
  for(auto ch:text)if(!alphabet.contains(ch))return {{"error","Unsupported JTTY character"}};
  int rate=request.value("rate").toInt(48000),symbols=0,nframes=0,status=-1;
  float frequency=float(request.value("frequency").toDouble(1500));
  if((rate!=12000 && rate!=48000) || !std::isfinite(frequency) || frequency<200 || frequency>4800)
    return {{"error","Invalid sample rate or audio frequency"}};
  auto message=text.toLatin1().leftJustified(80,' ');
  std::array<int,16*59> tones{};std::array<int,16> starts{};std::array<char,16*34> frames{};
  genjtty_text_c(message.data(),0,1,tones.data(),&symbols,frames.data(),&nframes,starts.data(),&status);
  if(status || symbols<1 || symbols>int(tones.size()))return {{"error","JTTY encoding failed"}};
  int nsps=rate*384/12000,nwave=symbols*nsps,complex=0;float fs=rate,bt=2.0f;
  std::vector<float> wave(nwave);
  gen_jttywave_(tones.data(),&symbols,&nsps,&bt,&fs,&frequency,wave.data(),wave.data(),&complex,&nwave);
  QByteArray pcm(nwave*2,Qt::Uninitialized);
  for(int i=0;i<nwave;++i)qToLittleEndian<qint16>(qBound(-32767,qRound(wave[i]*28000),32767),reinterpret_cast<uchar*>(pcm.data()+2*i));
  return {{"text",QString::fromLatin1(message).trimmed()},{"pcm",QString::fromLatin1(pcm.toBase64())},
          {"rate",rate},{"samples",nwave},{"frames",nframes}};
}
}
int main(int argc,char** argv) {
  QCoreApplication app(argc,argv);
#ifdef Q_OS_WIN
  _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
#endif
  Jtty::Decoder decoder;if(!decoder.valid())return 2;
  QVector<qint16> samples;qint64 first=0,session=0;
  decoder.begin(0,0,0);
  std::array<char,65536> buffer{};
  while(std::fgets(buffer.data(),buffer.size(),stdin)) {
    QByteArray line(buffer.data());
    if(!line.endsWith('\n'))return 3;
    QJsonParseError parse;auto document=QJsonDocument::fromJson(line,&parse);
    if(parse.error!=QJsonParseError::NoError || !document.isObject())return 4;
    auto request=document.object();auto op=request.value("op").toString();QJsonObject result;
    if(op=="reset") {
      decoder.end();decoder.takeUpdates();samples.clear();first=0;++session;decoder.begin(session,0,0);
    } else if(op=="encode")result=encode(request);
    else if(op=="audio") {
      auto bytes=QByteArray::fromBase64(request.value("pcm").toString().toLatin1());
      if(bytes.size()%2 || bytes.size()>24000){result["error"]="Invalid PCM block";}
      else {
        auto count=bytes.size()/2;auto old=samples.size();samples.resize(old+count);
        for(int i=0;i<count;++i)samples[old+i]=qFromLittleEndian<qint16>(reinterpret_cast<uchar const*>(bytes.constData()+2*i));
        int low=qBound(200,request.value("low").toInt(200),4800);
        int high=qBound(low+1,request.value("high").toInt(3000),5000);
        float center=qBound(200,request.value("center").toInt(1500),4800);
        auto steps=decoder.process(samples.constData(),samples.size(),first,first+samples.size(),64,low,high,center,100.0f);
        if(steps<0 || samples.size()>12000*180) {
          result["error"]="JTTY receive buffer overrun";decoder.end();samples.clear();first=0;decoder.begin(++session,0,0);
        } else {
          auto discard=qBound<qint64>(0,decoder.nextRequiredSample()-first,samples.size());
          if(discard){samples.remove(0,int(discard));first+=discard;}
        }
        QJsonArray updates;
        for(auto const& update:decoder.takeUpdates())updates.append(QJsonObject{
          {"id",QString::number(update.messageId)},{"text",update.text},{"frequency",update.frequency},
          {"seconds",update.startSeconds},{"terminal",int(update.terminal)},{"snr",update.snr}});
        result["updates"]=updates;
      }
    } else if(op=="quit")return 0;
    else result["error"]="Unknown JTTY operation";
    result["id"]=request.value("id");result["op"]=op;output(result);
  }
  return 0;
}
