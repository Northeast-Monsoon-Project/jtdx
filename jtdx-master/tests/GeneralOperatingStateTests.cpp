#include <QtTest>
#include "GeneralOperatingState.hpp"

class GeneralOperatingStateTests : public QObject {
  Q_OBJECT
private slots:
  void manualSessionRequired();
  void exactTargetAndTimeout();
  void pounceAndReply();
  void automaticAstronomyFollowsUpstreamModeRules();
};
void GeneralOperatingStateTests::manualSessionRequired()
{
  GeneralOperatingState state;QVERIFY(state.allowsTx());
  state.arm(GeneralOperatingState::Call,"K1ABC","operatorA|FT8");
  QVERIFY(!state.allowsTx());
  QVERIFY(!state.consider("K1ABC",false,true,false,false,"operatorA|FT8",1000,15));
  QVERIFY(!state.consider("K1ABC",false,true,false,true,"operatorB|FT8",1000,15));
  QVERIFY(!state.allowsTx());
  QVERIFY(state.consider("K1ABC",false,true,false,true,"operatorA|FT8",1000,15));
  QVERIFY(state.allowsTx());
  state.reset();
  QVERIFY(!state.consider("K1ABC",true,false,false,true,"operatorA|FT8",2000,15));
  QCOMPARE(state.mode(),GeneralOperatingState::Off);
}
void GeneralOperatingStateTests::exactTargetAndTimeout()
{
  GeneralOperatingState state;state.arm(GeneralOperatingState::Call,"K1ABC/P","ctx");
  QVERIFY(!state.consider("K1ABC",false,true,false,true,"ctx",1000,15));
  QVERIFY(!state.consider("K1ABCD/P",false,true,false,true,"ctx",1000,15));
  QVERIFY(state.consider("<K1ABC/P>",false,false,true,true,"ctx",1000,15));
  QVERIFY(!state.expired(120999));QVERIFY(state.expired(121000));
  QVERIFY(!state.consider("K1ABC/P",true,false,false,true,"ctx",2000,15));
  QVERIFY(!state.expired(1000000)); // Actual reply cancels the initial call timeout.
}
void GeneralOperatingStateTests::pounceAndReply()
{
  GeneralOperatingState state;state.arm(GeneralOperatingState::Pounce,"","ctx");
  QVERIFY(!state.consider("K2XYZ",false,false,true,true,"ctx",0,7.5));
  QVERIFY(state.consider("K2XYZ",false,true,false,true,"ctx",0,7.5));QCOMPARE(state.target(),QString("K2XYZ"));
  state.arm(GeneralOperatingState::Reply,"K3XYZ","ctx");
  QVERIFY(!state.consider("K3XYZ",false,false,false,true,"ctx",0,15));
  QVERIFY(!state.consider("K3XYZ",true,false,true,true,"ctx",0,15));
  QVERIFY(state.consider("K3XYZ",true,false,false,true,"ctx",0,15));
  QVERIFY(state.expired(120000));
}
void GeneralOperatingStateTests::automaticAstronomyFollowsUpstreamModeRules()
{
  using State=GeneralOperatingState;
  QCOMPARE(State::astroWindowAction("Echo",false,false),State::ShowAstroWindow);
  QCOMPARE(State::astroWindowAction("Q65",true,true),State::ShowAstroWindow);
  QCOMPARE(State::astroWindowAction("JT65",true,true),State::ShowAstroWindow);
  QCOMPARE(State::astroWindowAction("Q65",true,false),State::HideAstroWindow);
  QCOMPARE(State::astroWindowAction("MSK144",true,true),State::HideAstroWindow);
  QCOMPARE(State::astroWindowAction("FT8",true,true),State::HideAstroWindow);
  QCOMPARE(State::astroWindowAction("FT8",false,true),State::KeepAstroWindow);
}
QTEST_GUILESS_MAIN(GeneralOperatingStateTests)
#include "GeneralOperatingStateTests.moc"
