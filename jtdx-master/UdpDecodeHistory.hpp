// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef UDP_DECODE_HISTORY_HPP
#define UDP_DECODE_HISTORY_HPP
#include <QList>
#include <QRegularExpression>
#include <QStringList>
#include <QTime>
#include <cmath>

// Preserve the decoded row separately from its wire identity. Display filters,
// DXCC annotations and AP annotations must not change which station is selected.
struct UdpDecodeRecord {
  QString row,mode,message;
  QTime time;
  qint32 snr=0;
  float dt=0;
  quint32 frequency=0;
  bool lowConfidence=false,offAir=false;
  static QString identity(QString text) {
    static QRegularExpression annotation(QStringLiteral("(?:\\?\\s)?(?:a[0-9]|q[0-9][0-9]?)\\s*$"));
    text=text.trimmed();text.remove(annotation);text=text.trimmed();
    if(text.endsWith('?'))text.chop(1);
    return text.trimmed();
  }
  static bool parse(QString const& input,bool extended,bool disk,UdpDecodeRecord& result) {
    auto row=input.left(input.indexOf(QChar::Nbsp)).trimmed();
    auto parts=row.left(22).split(' ',Qt::SkipEmptyParts);
    if(parts.size()<5 || (parts[0].size()!=4 && parts[0].size()!=6))return false;
    UdpDecodeRecord value;value.row=row;value.offAir=disk;
    value.time=QTime::fromString(parts[0],parts[0].size()==6?"hhmmss":"hhmm");
    bool snrOk=false,dtOk=false,freqOk=false;
    value.snr=parts[1].toInt(&snrOk);value.dt=parts[2].toFloat(&dtOk);value.frequency=parts[3].toUInt(&freqOk);
    value.mode=parts[4];int start=parts[0].size()==6?23:21;
    auto payload=row.mid(start,extended?37:24);
    value.message=identity(payload);
    // Production Fortran uses a26,a1; older external fixtures use a24,a1.
    auto flag=row.mid(start+26,1);
    auto compactFlag=row.mid(start+24,1);
    auto uncertain=[](QString const& value){return value=="*" || value=="^" || value=="°" || value=="?";};
    value.lowConfidence=(!extended && (uncertain(flag) || uncertain(compactFlag))) || payload.contains('?');
    if(!value.time.isValid() || !snrOk || !dtOk || !freqOk || !std::isfinite(value.dt)
       || value.mode.size()!=1 || value.message.isEmpty())return false;
    result=value;return true;
  }
  bool matches(QTime stamp,qint32 report,float delta,quint32 offset,QString const& marker,QString const& text) const {
    return stamp==time && report==snr && std::isfinite(delta) && std::fabs(delta-dt)<0.0001f
      && offset==frequency && marker==mode && identity(text)==identity(message);
  }
};

struct UdpWsprRecord {
  QStringList fields;
  QTime time;
  qint32 snr=0,drift=0,power=0;
  float dt=0;
  double mhz=0;
  bool offAir=false;
  static bool parse(QStringList parts,bool disk,UdpWsprRecord& result) {
    if(parts.size()<7)return false;
    if(parts.size()==7)parts.insert(6,QString());
    UdpWsprRecord value;value.fields=parts;value.offAir=disk;
    value.time=QTime::fromString(parts[0],"hhmm");
    bool a=false,b=false,c=false,d=false,e=false;
    value.snr=parts[1].toInt(&a);value.dt=parts[2].toFloat(&b);value.mhz=parts[3].toDouble(&c);
    value.drift=parts[4].toInt(&d);value.power=parts[7].toInt(&e);
    if(!value.time.isValid() || !a || !b || !c || !d || !e || !std::isfinite(value.dt)
       || !std::isfinite(value.mhz) || value.mhz<=0 || value.mhz>100000 || value.power<0 || value.power>60)return false;
    result=value;return true;
  }
};

struct UdpReplayRow {QString row;bool offAir;bool wspr;};

class UdpDecodeHistory {
public:
  void setContext(QString const& context) {if(context_!=context){clear();context_=context;}}
  void clear() {records_.clear();wspr_.clear();}
  void append(UdpDecodeRecord const& record) {
    if(records_.size()>=4096)records_.removeFirst();
    records_.append(record);
  }
  QList<UdpDecodeRecord> const& records() const {return records_;}
  void append(UdpWsprRecord const& record) {
    if(wspr_.size()>=4096)wspr_.removeFirst();
    wspr_.append(record);
  }
  QList<UdpWsprRecord> const& wsprRecords() const {return wspr_;}
private:
  QString context_;
  QList<UdpDecodeRecord> records_;
  QList<UdpWsprRecord> wspr_;
};
#endif
