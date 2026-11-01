// SPDX-License-Identifier: GPL-3.0-or-later
#include "LogForwarder.hpp"
#include <QFile>
#include <QDateTime>
#include "moc_LogForwarder.cpp"

LogForwarder::LogForwarder(QString host, quint16 port, QByteArray payload,
                           QString path, QObject *parent)
  : QObject(parent), tracePath_(path)
{
  socket_.setReadBufferSize(4097);
  deadline_.setSingleShot(true);
  deadline_.setInterval(2500);
  connect(&deadline_, &QTimer::timeout, this, [this] {
    complete(tr("External logger did not acknowledge the QSO. Local logs are saved."));
  });
  connect(&socket_, &QTcpSocket::connected, this, [this, payload] {
    if (socket_.write(payload) != payload.size()) complete(socket_.errorString());
    else trace("Sent: " + QString::fromUtf8(payload));
  });
  connect(&socket_, &QTcpSocket::readyRead, this, [this] {
    response_ += socket_.readAll();
    if (response_.size() > 4096) {
      complete(tr("Invalid external logger response. Local logs are saved."));
      return;
    }
    // TCP may split the three-byte acknowledgement across packets.
    if (response_.size() < 3) return;
    if (response_.startsWith("NAK")) {
      complete(tr("External logger rejected the QSO. Local logs are saved.")
               + " " + QString::fromUtf8(response_));
    } else if (response_.startsWith("ACK")) complete({});
    else if (response_.contains('\n') || response_.contains('\r')) {
      complete(tr("Unexpected external logger response. Local logs are saved."));
    }
  });
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
  connect(&socket_, &QTcpSocket::errorOccurred, this, [this] {complete(socket_.errorString());});
#else
  connect(&socket_, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::error),
          this, [this] {complete(socket_.errorString());});
#endif
  connect(&socket_, &QTcpSocket::disconnected, this, [this] {
    if (!done_) complete(tr("External logger disconnected before acknowledgement. Local logs are saved."));
  });
  // Let the caller connect to finished() before DNS resolution can fail.
  QTimer::singleShot(0, this, [this, host, port] {
    trace("Connecting: " + host + ":" + QString::number(port));
    deadline_.start();
    socket_.connectToHost(host, port);
  });
}

void LogForwarder::trace(QString const& text)
{
  if (tracePath_.isEmpty()) return;
  QFile file(tracePath_);
  if (file.open(QIODevice::WriteOnly | QIODevice::Append)) {
    file.write((QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)
                + " " + text + '\n').toUtf8());
  }
}

void LogForwarder::complete(QString const& error)
{
  if (done_) return;
  done_ = true;
  deadline_.stop();
  trace(error.isEmpty() ? "Acknowledged" : error);
  socket_.abort();
  emit finished(error);
  deleteLater();
}
