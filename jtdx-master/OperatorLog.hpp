#ifndef JTDXF_OPERATOR_LOG_HPP
#define JTDXF_OPERATOR_LOG_HPP
#include <QDate>
#include <QDir>
#include <QMap>
#include <QStringList>
#include <QList>
namespace OperatorLog {
using Record=QMap<QString,QString>;
struct ImportResult { int imported=0, duplicates=0, archived=0, skipped=0; QStringList errors; };
QDir root();
QDir directory(QString const& callsign, QDir base=root());
QString quarterFile(QString const& callsign,QDate utcDate,QDir base=root());
QStringList files(QString const& callsign,QDir base=root());
QString compatibilityFile(QString const& callsign,QDir base=root());
// Union of quarterly history and the full compatibility log. Missing records
// and fields are filled in both directions; quarterly values win conflicts.
// Foreign STATION_CALLSIGN records are excluded only after their complete
// original files have been preserved under this operator's old/ directory.
ImportResult synchronize(QString const& callsign,QDir base=root());
namespace detail {
// Internal loader entry point: caller must hold directory/.operator-log.lock
// through synchronization, index construction and final fingerprint checking.
ImportResult synchronizeLocked(QString const& callsign,QDir base);
// Recover a final-message update before checking any cached log fingerprint.
// Caller must hold directory/.operator-log.lock.
bool recoverPendingUpdateLocked(QString const& callsign,QDir base,QString *error);
}
QList<Record> parse(QString const& text,QString *error=nullptr);
QList<Record> read(QString const& path,QString *error=nullptr);
QString serialize(Record const& record);
QString identity(Record const& record,QString const& fallbackCall={});
bool isQuarterFile(QString const& path);
ImportResult importFiles(QString callsign,QStringList paths,QDir base=root());
bool append(QString const& path,QString const& adif,QString *error=nullptr);
// Revise one already recorded QSO. Both records must have the same identity;
// original supplies the immutable start date/time and quarterly destination.
// Missing/ambiguous records fail instead of appending or matching by callsign.
// Updates the quarterly and compatibility copies under the same directory lock.
bool updateQso(QString const& callsign,Record const& original,Record const& replacement,
               QString *error=nullptr,QDir base=root());
}
#endif
