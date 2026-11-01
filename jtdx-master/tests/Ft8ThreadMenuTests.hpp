#include "Ft8ThreadPolicy.hpp"

void RegressionTest::ft8ThreadMenuLimits()
{
  auto saved=w->m_ft8threads;
  for(int cpu:{-1,1,2,4,8,12,24,32,64}) {
    w->setupFt8Threads(cpu);
    auto limit=Ft8ThreadPolicy::maximum(cpu);
    QVERIFY(w->ui->actionMTAuto->isVisible());
    w->setFt8Threads(24);QCOMPARE(w->m_ft8threads,limit);
    for(int count=1;count<=24;++count) {
      auto action=w->findChild<QAction*>(QString("actionMT%1").arg(count));QVERIFY(action);
      QCOMPARE(action->isVisible(),count<=limit);
      QCOMPARE(action->isEnabled(),count<=limit);
      w->setFt8Threads(0);action->trigger();
      QCOMPARE(w->m_ft8threads,count<=limit?count:0);
    }
    w->setFt8Threads(999);QCOMPARE(w->m_ft8threads,limit);
    w->setFt8Threads(-1);QCOMPARE(w->m_ft8threads,0);
    w->setFt8Threads(limit);w->ui->actionMTAuto->trigger();QCOMPARE(w->m_ft8threads,0);
  }
  w->setupFt8Threads(QThread::idealThreadCount());w->setFt8Threads(saved);
}
