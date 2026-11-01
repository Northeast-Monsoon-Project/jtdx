#include <QtTest>
#include <hamlib/rig.h>
#include <limits>
#include "RigPower.hpp"

class RigPowerTests : public QObject {
  Q_OBJECT
private slots:
  void meterPriority_data() {
    QTest::addColumn<int>("capabilities");
    QTest::addColumn<int>("expected");
    for (int caps = 0; caps < 8; ++caps) {
      auto expected = caps & 4 ? RigPower::Source::WattsMeter
                    : caps & 2 ? RigPower::Source::RelativeMeter
                    : caps & 1 ? RigPower::Source::Setting : RigPower::Source::Unavailable;
      QTest::newRow(qPrintable(QString::number(caps))) << caps << int(expected);
    }
  }
  void meterPriority() {
    QFETCH(int, capabilities);QFETCH(int, expected);
    QCOMPARE(int(RigPower::source(capabilities & 4, capabilities & 2, capabilities & 1)), expected);
  }
  void ft450UsesAdvertisedOutputMeter() {
    // Read capabilities only. Do not open a serial port or initialize a radio.
    rig_set_debug(RIG_DEBUG_NONE);
    QCOMPARE(rig_load_all_backends(), RIG_OK);
    auto caps = rig_get_caps_int(RIG_MODEL_FT450, RIG_CAPS_HAS_GET_LEVEL);
    QVERIFY(caps & RIG_LEVEL_RFPOWER_METER);
    auto selected = RigPower::source(caps & RIG_LEVEL_RFPOWER_METER_WATTS,
                                    caps & RIG_LEVEL_RFPOWER_METER, caps & RIG_LEVEL_RFPOWER);
    QVERIFY(selected != RigPower::Source::Setting);
    auto observed = RigPower::Source::Unavailable;
    float conversionInput = -1;
    auto get = [&](RigPower::Source source, double& value) {
      observed = source;
      value = source == RigPower::Source::WattsMeter ? 80
            : source == RigPower::Source::RelativeMeter ? 0.8 : 0.28;
      return true;
    };
    auto convert = [&](float relative, unsigned& milliwatts) {
      conversionInput = relative;milliwatts = 100000;return true;
    };
    QCOMPARE(RigPower::read(selected, get, convert), 80000u);
    QCOMPARE(int(observed), int(selected));
    QCOMPARE(conversionInput, selected == RigPower::Source::RelativeMeter ? 1.f : -1.f);
  }
  void meterFractionUsesMaximumNotControlCurve() {
    float conversionInput = -1;
    auto get = [](RigPower::Source, double& value) {value = 0.5;return true;};
    auto convert = [&](float relative, unsigned& milliwatts) {
      conversionInput = relative;milliwatts = unsigned(relative * relative * 100000);return true;
    };
    QCOMPARE(RigPower::read(RigPower::Source::RelativeMeter, get, convert), 50000u);
    QCOMPARE(conversionInput, 1.f);
    QCOMPARE(RigPower::read(RigPower::Source::Setting, get, convert), 25000u);
    QCOMPARE(conversionInput, 0.5f);
  }
  void wattsMeterDoesNotUseControlConversion() {
    int conversions = 0;
    auto get = [](RigPower::Source, double& value) {value = 28.125;return true;};
    auto convert = [&](float, unsigned&) {++conversions;return false;};
    QCOMPARE(RigPower::read(RigPower::Source::WattsMeter, get, convert), 28125u);
    QCOMPARE(conversions, 0);
  }
  void failedMeterDoesNotSubstituteSetting() {
    int readings = 0, conversions = 0;
    auto get = [&](RigPower::Source, double& value) {++readings;value = 80;return false;};
    auto convert = [&](float, unsigned&) {++conversions;return true;};
    QCOMPARE(RigPower::read(RigPower::source(true, true, true), get, convert), 0u);
    QCOMPARE(readings, 1);QCOMPARE(conversions, 0);
    QCOMPARE(RigPower::read(RigPower::source(false, false, false), get, convert), 0u);
    QCOMPARE(readings, 1);QCOMPARE(conversions, 0);
  }
  void invalidValuesAreUnavailable() {
    for (auto source : {RigPower::Source::WattsMeter, RigPower::Source::RelativeMeter, RigPower::Source::Setting}) {
      for (double value : {-1., std::numeric_limits<double>::quiet_NaN(),
                           std::numeric_limits<double>::infinity(),
                           source == RigPower::Source::WattsMeter ? 1000001. : 1.01}) {
        int conversions = 0;
        auto get = [value](RigPower::Source, double& result) {result = value;return true;};
        auto convert = [&](float, unsigned&) {++conversions;return true;};
        QCOMPARE(RigPower::read(source, get, convert), 0u);
        QCOMPARE(conversions, 0);
      }
    }
  }
  void failedConversionIsUnavailable() {
    auto get = [](RigPower::Source, double& value) {value = 0.8;return true;};
    auto convert = [](float, unsigned& result) {result = 80000;return false;};
    QCOMPARE(RigPower::read(RigPower::Source::RelativeMeter, get, convert), 0u);
    QCOMPARE(RigPower::read(RigPower::Source::Setting, get, convert), 0u);
  }
};
QTEST_GUILESS_MAIN(RigPowerTests)
#include "RigPowerTests.moc"
