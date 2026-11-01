// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef FM_UPSTREAM_CONTROLLER_HPP
#define FM_UPSTREAM_CONTROLLER_HPP
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QTemporaryDir>
#include <QJsonObject>
#include <QList>

// The WSJT-X backend has independent Fortran globals and averaging state.
// Only the GUI owns the radio and transmission permission.
class UpstreamController final : public QObject {
  Q_OBJECT
public:
  explicit UpstreamController(QObject *parent=nullptr, QString program={});
  ~UpstreamController() override;
  void setContext(QString const&);
  void stop();
  bool decode(QByteArray pcm, QJsonObject parameters);
  bool encode(QString message, QJsonObject parameters);
  void astronomy(QJsonObject parameters);
  void cancelTransmit();
  bool busy() const {return busy_;}
  bool encoding() const;
  QString context() const {return context_;}
signals:
  void ready(QJsonObject capabilities);
  void decoded(QJsonObject result, QJsonObject request);
  void echoData(QJsonObject result, QJsonObject request);
  void decodeFinished(QJsonObject request);
  void encoded(QString message, QByteArray pcm, QJsonObject result);
  void astronomicalData(QJsonObject result);
  void error(QString message);
private:
  friend class UpstreamControllerTests;
  struct Job {QJsonObject request; QByteArray pcm; quint64 txRevision=0;};
  bool enqueue(Job);
  void pump();
  void receive();
  void complete();
  void fail(QString const&);
  void clearFiles();
  void clearBackendFiles();
  // Destruction order keeps the parent-owned files until QProcess has exited.
  QTemporaryDir directory_;
  QProcess process_;
  QTimer timeout_;
  QString program_,context_,inputPath_,outputPath_;
  QByteArray input_;
  QList<Job> queue_;
  Job current_;
  quint64 serial_=0,txRevision_=0;
  bool busy_=false,hello_=false,stopping_=false;
};
#endif
