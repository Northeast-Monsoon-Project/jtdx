// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef JTDXF_LOG_FORWARDER_HPP
#define JTDXF_LOG_FORWARDER_HPP
#include <QObject>
#include <QTcpSocket>
#include <QTimer>
class LogForwarder final : public QObject {
  Q_OBJECT
public:
  LogForwarder(QString host,quint16 port,QByteArray payload,QString trace={},QObject* parent=nullptr);
signals:
  void finished(QString error);
private:
  void complete(QString const& error);
  void trace(QString const& text);
  QTcpSocket socket_;QTimer deadline_;QByteArray response_;QString tracePath_;bool done_=false;
};
#endif
