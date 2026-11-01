#include "NtpClient.hpp"
#include <QDateTime>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QUrl>
#include <QtEndian>
#include <cmath>

namespace {
constexpr qint64 epoch=2208988800LL;
double readTimestamp(QByteArray const& p,int at,qint64 nearMs) {
  auto sec=qFromBigEndian<quint32>(reinterpret_cast<uchar const*>(p.constData()+at));
  auto frac=qFromBigEndian<quint32>(reinterpret_cast<uchar const*>(p.constData()+at+4));
  double value=(qint64(sec)-epoch)*1000.0+frac*(1000.0/4294967296.0);
  // NTP's 32-bit seconds wrap in 2036; choose the era nearest the local date.
  constexpr double era=4294967296000.0;
  return value+std::round((nearMs-value)/era)*era;
}
}
QString NtpClient::normalizeServer(QString const& input) {
  QString host=input.trimmed();QHostAddress ip;
  if(ip.setAddress(host)) return ip.toString();
  host=QString::fromLatin1(QUrl::toAce(host)).toLower();
  if(host.endsWith('.')) host.chop(1);
  if(host.isEmpty() || host.size()>253) return {};
  static QRegularExpression label("^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$");
  for(auto const& part:host.split('.')) if(!label.match(part).hasMatch()) return {};
  return host;
}
QByteArray NtpClient::timestamp(qint64 ms) {
  QByteArray b(8,0);
  qToBigEndian<quint32>(quint32(ms/1000+epoch),reinterpret_cast<uchar*>(b.data()));
  qToBigEndian<quint32>(quint32((ms%1000)*4294967296ULL/1000),reinterpret_cast<uchar*>(b.data()+4));
  return b;
}
bool NtpClient::decode(QByteArray const& p,QByteArray const& id,qint64 t1,qint64 t4,qint64 elapsed,NtpSample *out,QString *error) {
  auto reject=[&](char const *message){if(error)*error=tr(message);return false;};
  if(p.size()<48 || p.size()>1024 || id.size()!=8) return reject(QT_TRANSLATE_NOOP("NtpClient","Invalid NTP packet length."));
  auto flags=quint8(p[0]);auto stratum=quint8(p[1]);
  if((flags&7)!=4 || ((flags>>3)&7)<3 || ((flags>>3)&7)>4 || (flags>>6)==3 || stratum<1 || stratum>15)
    return reject(QT_TRANSLATE_NOOP("NtpClient","The NTP server is not synchronized or refused the request."));
  if(p.mid(24,8)!=id || p.mid(32,8)==QByteArray(8,0) || p.mid(40,8)==QByteArray(8,0))
    return reject(QT_TRANSLATE_NOOP("NtpClient","The NTP response does not match this request."));
  if(elapsed<0 || elapsed>3000 || std::abs(double(t4-t1-elapsed))>100)
    return reject(QT_TRANSLATE_NOOP("NtpClient","The clock changed during the NTP request, or the response was too slow."));
  double t2=readTimestamp(p,32,t4),t3=readTimestamp(p,40,t4);
  double delay=double(elapsed)-(t3-t2);
  auto dispersion=qFromBigEndian<quint32>(reinterpret_cast<uchar const*>(p.constData()+8))/65.536;
  if(t3<t2 || delay < -2 || delay>2000 || dispersion>5000)
    return reject(QT_TRANSLATE_NOOP("NtpClient","The NTP response has excessive delay or uncertainty."));
  double offset=((t2-t1)+(t3-t4))/2;
  auto utc=qRound64(t4+offset);
  auto date=QDateTime::fromMSecsSinceEpoch(utc,Qt::UTC).date();
  if(date.year()<2000 || date.year()>2100) return reject(QT_TRANSLATE_NOOP("NtpClient","The NTP date is outside the supported range."));
  if(out) *out={utc,offset,qMax(0.0,delay)};
  if(error) error->clear();
  return true;
}
NtpClient::NtpClient(QObject *parent):QObject(parent) {
  timeout_.setSingleShot(true);
  connect(&timeout_,&QTimer::timeout,this,[this]{fail(tr("NTP request timed out."));});
  connect(&socket_,QOverload<QAbstractSocket::SocketError>::of(&QUdpSocket::error),this,[this]{if(running_)fail(socket_.errorString());});
  connect(&socket_,&QUdpSocket::connected,this,[this]{
    if(!running_)return;
    QByteArray p(48,0);p[0]=0x23;
    sentMs_=QDateTime::currentMSecsSinceEpoch();elapsed_.start();
    requestId_=timestamp(sentMs_);
    // Random low fractional bits bind each reply to an unpredictable request.
    auto random=QRandomGenerator::system()->generate();
    requestId_[6]=char(random>>8);requestId_[7]=char(random);
    p.replace(40,8,requestId_);
    if(socket_.write(p)!=p.size()) fail(socket_.errorString());
  });
  connect(&socket_,&QUdpSocket::readyRead,this,[this]{
    while(running_ && socket_.hasPendingDatagrams()) {
      QByteArray p(int(qMin<qint64>(socket_.pendingDatagramSize(),1025)),0);
      auto n=socket_.readDatagram(p.data(),p.size());if(n<0){fail(socket_.errorString());return;}p.resize(int(n));
      NtpSample sample;QString error;
      if(!decode(p,requestId_,sentMs_,QDateTime::currentMSecsSinceEpoch(),elapsed_.elapsed(),&sample,&error)){fail(error);return;}
      cancel();emit received(sample);
    }
  });
}
void NtpClient::query(QString const& host,quint16 port) {
  cancel();auto normalized=normalizeServer(host);
  if(normalized.isEmpty()){emit failed(tr("Enter a valid NTP hostname or IP address."));return;}
  running_=true;timeout_.start(10000);socket_.connectToHost(normalized,port);
}
void NtpClient::cancel(){running_=false;timeout_.stop();socket_.abort();requestId_.clear();}
void NtpClient::fail(QString const& message){cancel();emit failed(message);}
