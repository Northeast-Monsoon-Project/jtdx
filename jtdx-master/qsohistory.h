/*
 * maintains QSO Histories and autoselect
 * Created by Arvo ES1JA 
 */

#ifndef __QSOHISTORY_H
#define __QSOHISTORY_H

#include <QList>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QElapsedTimer>
#include <functional>
#include "Radio.hpp"
#include <QRegularExpression>
#include <QtMath>
#include "JTDXDateTime.h"
class QsoHistory
{
 public:
    // The optional clock returns monotonic milliseconds and is used by tests.
    explicit QsoHistory(std::function<qint64()> monotonicNow = {});
//                  0     1    2    3      4      5        6        7         8         9   10   11     12     13   14   15   16
	enum Status {NONE, RFIN, RCQ, SCQ, RCALL, SCALL, RREPORT, SREPORT, RRREPORT, SRREPORT, RRR, SRR, RRR73, SRR73, R73, S73, FIN};
	void init();
	void message(QString const& callsign, Status status, int priority, QString const& param, QString const& tyyp, QString const& continent, QString const& mpx, unsigned time, QString const& rep, int freq,  QString const& mode);
	void complete(QString const& callsign);
	void rx(QString const& callsign, int freq);
	void time(unsigned time);
	void owndata (QString const& mycontinent, QString const& myprefix, QString const& mygrid, bool strictdirCQ);
	Status status(QString const& callsign, QString &grid);
	Status autoseq(QString &callsign, QString &grid, QString &rep, int &rx, int &tx, unsigned &time, int &count, int &prio, QString &mode);
	Status log_data(QString const& callsign, unsigned &time, QString &rrep, QString &srep);
	int remove(QString const& callsign);		
	QStringList calls() const {QStringList out;for(auto const& q:_data)out.append(q.call);return out;}
	void discardDecodedCall(QString const& call) {_data.remove(Radio::base_callsign(call));}
	int blacklist(QString const& callsign);
	void calllist(QString const& callsign,int level, unsigned time);
	int reset_count(QString const& callsign,Status status = NONE);
	int sent_count(QString const& callsign) const;
    // Restrict automatic outgoing selection only, never replies or manual calls.
    void deferOutgoingCall(QString const& callsign);
    bool outgoingCallDeferred(QString const& callsign) const;
	JTDXDateTime * jtdxtime;
 private:
	QRegularExpression _gridRe = QRegularExpression("^[A-R]{2}[0-9]{2}[A-X]{2}[0-9]{2}[A-X]{2}$");

 	struct latlng {
 	  double lat;
 	  double lng;
 	};

 	struct QSO
 	{
	  QString	call,grid,r_rep,s_rep,tyyp,continent,mpx,mode;
	  Status status = NONE, srx_c = NONE, srx_p = NONE, stx_c = NONE, stx_p = NONE;
	  unsigned b_time = 0, time = 0;
	  int distance = 0, rx = 0, tx = 0, count = 0, priority = 0;
	   	
 	};

 	struct CALLED
 	{
	  int rep;
	  unsigned time;
 	};

	QHash<QString, QSO> _data;
	QHash<QString, int> _blackdata;
	QHash<QString, CALLED> _calldata;
    QElapsedTimer _outgoingClock;
    std::function<qint64()> _monotonicNow;
    QHash<QString, qint64> _outgoingDeferredUntil;
    qint64 outgoingNow() const;
	bool _working = false;
	bool as_active = false;
	bool _strictdirCQ = false;
	QSO _CQ;
	latlng _mylatlng {0, 0};
	int a_init = 0;
	int b_init = 0;
	int dist = 0;
	unsigned max_r_time = 0, algo = 0;
	QString myprefix_="" ,mycontinent_="" ,mygrid_="" ,Rrep = "-60" ; 

        double rad_3 = M_PI/180.0; 
        double rad_0 = 20.0 * rad_3;
        double rad_1 = 10.0 * rad_3;
        double rad_2 = 2.0 * rad_3;
        double rad_4 = rad_3 / 12.0;
        double rad_5 = rad_3 / 24.0;
        double rad_6 = rad_3 / 120.0;
        double rad_7 = rad_3 / 240.0;
        double rad_8 = rad_3 / 2880.0;
        double rad_9 = rad_3 / 5760.0;
         
 	latlng fromQth(QString const& qth);
 	int Distance(latlng latlng1,latlng latlng2);
};

#endif


