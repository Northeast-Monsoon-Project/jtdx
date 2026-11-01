#ifndef JTDXF_OPERATOR_LOG_LOADER_HPP
#define JTDXF_OPERATOR_LOG_LOADER_HPP

#include "OperatorLog.hpp"
#include "logbook/logbook.h"
#include <QMutex>

// Owned by the main window, but load() runs in its background worker. Copies of
// LogBook share Qt containers until modified; no cached QObject crosses threads.
class OperatorLogLoader {
public:
  explicit OperatorLogLoader(int recordBudget=100000):recordBudget_(qMax(1,recordBudget)) {}
  struct Result {
    LogBook book;
    QString call,grid,date,error;
    bool cacheHit=false,synchronized=false;
    qint64 elapsedMs=0;
  };
  Result load(QString const& call,QString const& grid,QString const& date,
              QDir base=OperatorLog::root());
  void clear();

private:
  struct Entry {
    QString root,call,grid,date;
    QByteArray logs,databases;
    LogBook book;
    int qsoCount=0;
  };
  QMutex mutex_;
  QList<Entry> entries_; // most recently used first; at most four snapshots
  int recordBudget_; // retain the latest snapshot even when it alone exceeds this
};

#endif
