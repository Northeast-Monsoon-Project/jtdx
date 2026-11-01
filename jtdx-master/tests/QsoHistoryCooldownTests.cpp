#include <QtTest>
#include "qsohistory.h"

namespace {
constexpr qint64 cooldownMs = 8 * 60 * 1000;

void receive(QsoHistory& history, QString const& call, QsoHistory::Status status,
             unsigned utc = 15, QString const& report = "-10", int priority = 5,
             QString const& parameter = "FN20")
{
  history.time(utc);
  history.message(call, status, priority, parameter, "", "NA", "K", utc, report, 1500, "FT8");
}

QsoHistory::Status select(QsoHistory& history, QString& call)
{
  QString grid, report, mode;
  int rx = 0, tx = 0, count = 0, priority = 0;
  unsigned flags = 1 | 64 | 128;
  return history.autoseq(call, grid, report, rx, tx, flags, count, priority, mode);
}
}

class QsoHistoryCooldownTests : public QObject {
  Q_OBJECT
private slots:
  void eightMinuteBoundaryAndNormalization()
  {
    qint64 now = 12345;
    QsoHistory history([&] { return now; });
    history.init();
    history.deferOutgoingCall(" k1abc/p ");
    QVERIFY(history.outgoingCallDeferred("K1ABC"));
    QVERIFY(history.outgoingCallDeferred("EA8/K1ABC"));
    QVERIFY(!history.outgoingCallDeferred("K2ABC"));
    now += cooldownMs - 1;
    QVERIFY(history.outgoingCallDeferred("K1ABC/QRP"));
    ++now;
    QVERIFY(!history.outgoingCallDeferred("K1ABC"));
  }

  void defaultClockAndEmptyCalls()
  {
    QsoHistory history;
    history.deferOutgoingCall("K1ABC");
    QVERIFY(!history.outgoingCallDeferred("K1ABC"));
    history.init();
    history.deferOutgoingCall("   ");
    QVERIFY(!history.outgoingCallDeferred(""));
    history.deferOutgoingCall("K1ABC");
    QVERIFY(history.outgoingCallDeferred("K1ABC"));
  }

  void utcAndHistoryResetsDoNotShortenCooldown()
  {
    qint64 now = 500;
    QsoHistory history([&] { return now; });
    history.init();
    history.deferOutgoingCall("K1ABC");
    for (unsigned utc : {86399u, 0u, 36000u, 1u}) {
      history.time(utc); // Midnight and clock corrections cannot expire it.
      QVERIFY(history.outgoingCallDeferred("K1ABC"));
    }
    history.remove("K1ABC");
    history.init();
    QVERIFY(history.outgoingCallDeferred("K1ABC"));
    QsoHistory restarted([&] { return now; });
    restarted.init();
    QVERIFY(!restarted.outgoingCallDeferred("K1ABC"));
    now += cooldownMs;
    QVERIFY(!history.outgoingCallDeferred("K1ABC"));
  }

  void outgoingCandidates_data()
  {
    QTest::addColumn<int>("status");
    QTest::addColumn<QString>("parameter");
    QTest::newRow("CQ") << int(QsoHistory::RCQ) << QString("FN20");
    // The decoded-message path maps all three third-party endings to RFIN.
    QTest::newRow("73") << int(QsoHistory::RFIN) << QString("73");
    QTest::newRow("RR73") << int(QsoHistory::RFIN) << QString("RR73");
    QTest::newRow("RRR") << int(QsoHistory::RFIN) << QString("RRR");
  }

  void outgoingCandidates()
  {
    QFETCH(int, status); QFETCH(QString, parameter);
    qint64 now = 1000;
    QsoHistory history([&] { return now; });
    history.init();
    history.calllist("K1ABC", -20, 15);
    history.deferOutgoingCall("K1ABC");
    receive(history, "K1ABC/P", QsoHistory::Status(status), 15, "-1", 28, parameter);
    QString call;
    QCOMPARE(select(history, call), QsoHistory::NONE);
    QVERIFY(call.isEmpty()); // Stronger signal and highest rank cannot bypass it.
    now += cooldownMs - 1;
    receive(history, "K1ABC/P", QsoHistory::Status(status), 30, "0", 28, parameter);
    QCOMPARE(select(history, call), QsoHistory::NONE);
    ++now;
    receive(history, "K1ABC/P", QsoHistory::Status(status), 45, "0", 28, parameter);
    QCOMPARE(select(history, call), QsoHistory::Status(status));
    QCOMPARE(call, QString("K1ABC/P"));
  }

  void incomingCandidatesBypassCooldown_data()
  {
    QTest::addColumn<int>("status");
    QTest::newRow("direct-call") << int(QsoHistory::RCALL);
    QTest::newRow("report") << int(QsoHistory::RREPORT);
    QTest::newRow("R-report") << int(QsoHistory::RRREPORT);
    QTest::newRow("RRR") << int(QsoHistory::RRR);
    QTest::newRow("RR73") << int(QsoHistory::RRR73);
  }

  void incomingCandidatesBypassCooldown()
  {
    QFETCH(int, status);
    qint64 now = 10;
    QsoHistory history([&] { return now; });
    history.init();
    history.deferOutgoingCall("K1ABC");
    if (status >= QsoHistory::RRREPORT) {
      receive(history, "K1ABC/P", QsoHistory::RCALL);
      receive(history, "K1ABC/P", QsoHistory::SREPORT, 15, "-10", 5, "-12");
      receive(history, "K1ABC/P", QsoHistory::RRREPORT, 15, "-10", 5, "R-10");
    }
    receive(history, "K1ABC/P", QsoHistory::Status(status), 15, "-10", 5, "-10");
    QString call;
    QCOMPARE(select(history, call), QsoHistory::Status(status));
    QCOMPARE(call, QString("K1ABC/P"));
    QVERIFY(history.outgoingCallDeferred(call));
  }

  void staleIncomingCallAlsoBypassesCooldown()
  {
    qint64 now = 0;
    QsoHistory history([&] { return now; });
    history.init();
    history.deferOutgoingCall("K1ABC");
    receive(history, "K1ABC", QsoHistory::RCALL, 86385);
    history.time(15); // Exercise the previous receive-cycle candidate pass.
    QString call;
    QCOMPARE(select(history, call), QsoHistory::RCALL);
    QCOMPARE(call, QString("K1ABC"));
  }

  void explicitSelectionAndOtherStationsAreUnaffected()
  {
    qint64 now = 0;
    QsoHistory history([&] { return now; });
    history.init();
    history.deferOutgoingCall("K1ABC");
    receive(history, "K1ABC", QsoHistory::RCQ, 15, "0", 28);
    QString call = "K1ABC";
    QCOMPARE(select(history, call), QsoHistory::RCQ); // Manual/current call lookup.
    receive(history, "K2ABC", QsoHistory::RCQ, 15, "-20", 5);
    call.clear();
    QCOMPARE(select(history, call), QsoHistory::RCQ);
    QCOMPARE(call, QString("K2ABC"));
  }

  void legacyCalllistAndBlacklistStillApply()
  {
    qint64 now = 0;
    QsoHistory history([&] { return now; });
    history.init();
    history.calllist("K1ABC", -5, 600);
    history.deferOutgoingCall("K1ABC");
    now += cooldownMs;
    receive(history, "K1ABC", QsoHistory::RCQ, 600, "-10");
    QString call;
    QCOMPARE(select(history, call), QsoHistory::NONE); // Existing calllist rule.
    receive(history, "K1ABC", QsoHistory::RCQ, 600, "-1");
    QCOMPARE(select(history, call), QsoHistory::RCQ);
    history.blacklist("K1ABC");
    receive(history, "K1ABC", QsoHistory::RCALL, 600);
    call.clear();
    QCOMPARE(select(history, call), QsoHistory::NONE); // Existing blacklist wins.
  }

  void newFailureStartsAnotherFullCooldown()
  {
    qint64 now = 0;
    QsoHistory history([&] { return now; });
    history.init();
    history.deferOutgoingCall("K1ABC");
    now += cooldownMs;
    QVERIFY(!history.outgoingCallDeferred("K1ABC"));
    history.deferOutgoingCall("K1ABC");
    now += cooldownMs - 1;
    QVERIFY(history.outgoingCallDeferred("K1ABC"));
    ++now;
    QVERIFY(!history.outgoingCallDeferred("K1ABC"));
  }
};

QTEST_GUILESS_MAIN(QsoHistoryCooldownTests)
#include "QsoHistoryCooldownTests.moc"
