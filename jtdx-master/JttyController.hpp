// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef FM_JTTY_CONTROLLER_HPP
#define FM_JTTY_CONTROLLER_HPP
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QJsonObject>
class JttyController final : public QObject {
  Q_OBJECT
public:
  explicit JttyController(QObject* parent=nullptr);
  ~JttyController() override;
  void setActive(bool);
  void reset();
  void audio(QByteArray const& pcm);
  bool encode(QString const& text,int frequency);
  void cancelTransmit();
  void setReceiveRange(int low,int high,int center);
  bool encoding() const {return encodePending_;}
signals:
  void received(QJsonObject update);
  void encoded(QString text,QByteArray pcm);
  void error(QString message);
private:
  void pump();
  void write(QJsonObject);
  void fail(QString const&);
  QProcess process_;QTimer deadline_;QByteArray input_,audio_;
  QJsonObject encode_;quint64 serial_=0,inflight_=0,encodeId_=0;
  bool active_=false,busy_=false,reset_=true,encodePending_=false;
  int low_=200,high_=3000,center_=1500;
};
#endif
