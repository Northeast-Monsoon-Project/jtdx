#ifndef JTDXF_BLACKLIST_HPP
#define JTDXF_BLACKLIST_HPP

#include <QDialog>
#include <QCoreApplication>
#include <QDir>
#include <QFileSystemWatcher>
#include <QList>
#include <QObject>
#include <QSet>
#include <QTimer>

class QLabel;
class QListWidget;

class Blacklist : public QObject {
  Q_OBJECT
public:
  struct Entry {QString call,note;};
  explicit Blacklist(QObject* parent=nullptr);
  bool load(QString const& operatorCall,QDir const& operatorDirectory,QString* error=nullptr);
  bool reload(QString* error=nullptr);
  bool save(QList<Entry> const& entries,QString* error=nullptr,QByteArray const& expectedRevision={});
  QList<Entry> entries() const {return entries_;}
  bool matches(QString callsign) const;
  bool ready() const {return ready_;}
  QString filePath() const {return path_;}
  QString error() const {return error_;}
  QByteArray contentRevision() const; // binds the operator and path as well as bytes
  static QString validate(QList<Entry> const& entries);
  static QString matchingHelp();
signals:
  void changed();
  void loadError(QString const& message);
private:
  void watch();
  void rebuild();
  bool fail(QString const& message,QString* error);
  QString fileStamp() const;
  QString path_,operatorCall_,error_,stamp_;
  QByteArray fingerprint_;
  QList<Entry> entries_;
  QSet<QString> baseCalls_,exactCalls_;
  QFileSystemWatcher watcher_;
  QTimer reloadTimer_;
  bool ready_=false;
};

class BlacklistDialog : public QDialog {
  Q_DECLARE_TR_FUNCTIONS(BlacklistDialog)
public:
  explicit BlacklistDialog(Blacklist* blacklist,QWidget* parent=nullptr);
private:
  void edit(int index);
  void refresh(int selected=-1);
  Blacklist* blacklist_;
  QList<Blacklist::Entry> entries_;
  QByteArray revision_;
  QListWidget* list_;
  QLabel* errorLabel_;
};

#endif
