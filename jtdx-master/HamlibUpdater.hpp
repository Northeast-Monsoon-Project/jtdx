#ifndef HAMLIB_UPDATER_HPP
#define HAMLIB_UPDATER_HPP
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QNetworkAccessManager>
#include <QSaveFile>
#include <QLockFile>
#include <QTimer>
#include <memory>
class QNetworkReply;
class HamlibUpdater final: public QObject {
  Q_OBJECT
public:
  explicit HamlibUpdater(QObject *parent=nullptr);
  ~HamlibUpdater() override;
  QString currentVersion() const;
  QString backupVersion() const;
  bool canUpdate() const;
  QString availabilityReason() const;
  bool busy() const {return busy_;}
  QString readyVersion() const {return readyVersion_;}
public slots:
  void downloadUpdate();
  void rollback();
  void restartApplication();
signals:
  void progress(qint64 received,qint64 total);
  void statusChanged(QString const& message);
  void failed(QString const& message);
  void readyToRestart(QString const& version,bool rollback);
  void restartRequested();
private:
  QString path(QString const& name) const;
  bool begin();
  void fail(QString const& message);
  void probe(QString const& candidate,bool rollback);
  QNetworkAccessManager network_;
  QPointer<QNetworkReply> reply_;
  std::unique_ptr<QSaveFile> output_;
  std::unique_ptr<QLockFile> lock_;
  QProcess probe_;
  QTimer timeout_;
  bool busy_=false, rollback_=false;
  QString candidate_,readyVersion_,readyHash_,activeHash_;
};
#endif
