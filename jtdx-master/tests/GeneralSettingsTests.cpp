#include <QtTest>
#include <QSettings>
#include <QTemporaryDir>
#include <QDialog>

#include "WSJTXGeneralSettings.hpp"
#include "ui_Configuration.h"

class GeneralSettingsTests : public QObject {
  Q_OBJECT
private slots:
  void doubleClickTxDoesNotInheritLegacyQuickCall() {
    QTemporaryDir dir;QSettings settings(dir.filePath("quick.ini"),QSettings::IniFormat);
    settings.setValue("Common/QuickCall",false);
    settings.setValue("Configuration/QuickCall",false);
    WSJTXGeneralSettings value;value.readRoot(settings);QVERIFY(value.quickCall);
    settings.setValue("Configuration/DoubleClickEnableTx",true);
    value.readRoot(settings);QVERIFY(value.quickCall);
    settings.beginGroup("Configuration");value.write(settings);settings.endGroup();settings.sync();
    QSettings reopened(settings.fileName(),QSettings::IniFormat);
    value.readRoot(reopened);QVERIFY(value.quickCall);
    reopened.setValue("Configuration/DoubleClickEnableTx",false);
    value.readRoot(reopened);QVERIFY(!value.quickCall);
  }
  void safeDefaultsAndRoundTrip() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
    settings.beginGroup("Configuration");
    WSJTXGeneralSettings value; value.read(settings);
    QVERIFY(value.dxcc); QVERIFY(value.detailedBlank);
    QVERIFY(value.quickCall); QVERIFY(!value.disableTxAfter73); QVERIFY(!value.fullDuplex);
    QVERIFY(!value.checkSwr); QVERIFY(value.eraseBandActivity);
    QVERIFY(value.rxToTxAfterQso); QVERIFY(value.clearDxCall); QVERIFY(value.clearDxGrid);
    value.write(settings);
    auto keys = settings.allKeys();
    QCOMPARE(keys.size(), 32); // 30 booleans and the two bounded spacing values.
    // Exercise every persisted boolean, not just fields with true defaults.
    for (auto const& key : keys)
      if (key != "AlignSteps" && key != "AlignSteps2") settings.setValue(key, !settings.value(key).toBool());
    settings.setValue("AlignSteps", -2); settings.setValue("AlignSteps2", 5);
    WSJTXGeneralSettings changed; changed.read(settings);
    QVariantMap before;
    for (auto const& key : keys) before.insert(key, settings.value(key));
    settings.clear(); changed.write(settings);
    for (auto const& key : keys) QCOMPARE(settings.value(key), before.value(key));
    QVERIFY(!changed.quickCall); QVERIFY(changed.fullDuplex); QVERIFY(changed.checkSwr);
  }

  void cleanupDefaultsMigrateOnceAndPreserveLaterChoices() {
    QTemporaryDir dir; QVERIFY(dir.isValid());
    QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
    // Both a fresh installation and an upgraded V3.6.0 configuration enable
    // the four options. Conflicting older Common defaults cannot win.
    for (bool existing : {false, true}) {
      settings.clear();
      settings.setValue("Common/EraseWindowsAtBandChange", false);
      settings.setValue("Configuration/ClearCallGrid", false);
      settings.setValue("Configuration/QuickCall", false);
      settings.setValue("Configuration/TxWatchdogTimer", 999);
      if (existing)
        for (auto key : {"set_RXtoTX", "erase_BandActivity", "clear_DXcall", "clear_DXgrid"})
          settings.setValue(QString("Configuration/") + key, false);
      WSJTXGeneralSettings::migrateDefaultsV361(settings);
      QVERIFY(settings.group().isEmpty());
      QVERIFY(settings.value("Configuration/QsoCleanupDefaultsV361").toBool());
      WSJTXGeneralSettings value; value.readRoot(settings);
      QVERIFY(value.rxToTxAfterQso && value.eraseBandActivity && value.clearDxCall && value.clearDxGrid);
      QVERIFY(value.quickCall);
      QCOMPARE(settings.value("Configuration/TxWatchdogTimer").toInt(), 999);

      value.rxToTxAfterQso = value.eraseBandActivity = value.clearDxCall = value.clearDxGrid = false;
      settings.beginGroup("Configuration"); value.write(settings); settings.endGroup();
      settings.sync();
      // Reload through a new QSettings object to exercise a real restart.
      QSettings restarted(settings.fileName(), QSettings::IniFormat);
      WSJTXGeneralSettings::migrateDefaultsV361(restarted);
      value.readRoot(restarted);
      QVERIFY(!value.rxToTxAfterQso && !value.eraseBandActivity && !value.clearDxCall && !value.clearDxGrid);
      QCOMPARE(restarted.value("Configuration/TxWatchdogTimer").toInt(), 999);
    }
  }

  void legacyMigrationAndExplicitNewValuesWin() {
    QTemporaryDir dir; QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
    settings.beginGroup("Configuration");
    settings.setValue("ClearCallGrid", true); settings.setValue("TuneTimer", 0);
    WSJTXGeneralSettings value; value.read(settings, true, false, true);
    QVERIFY(value.disableTxAfter73); QVERIFY(!value.eraseBandActivity); QVERIFY(value.fullDuplex);
    QVERIFY(value.clearDxCall && value.clearDxGrid); QVERIFY(!value.tuneWatchdog);
    settings.setValue("73TxDisable", false); settings.setValue("erase_BandActivity", true);
    settings.setValue("FullDuplexMode", false); settings.setValue("clear_DXcall", false);
    settings.setValue("TuneWatchdog", true);
    value.read(settings, true, false, true);
    QVERIFY(!value.disableTxAfter73); QVERIFY(value.eraseBandActivity); QVERIFY(!value.fullDuplex);
    QVERIFY(!value.clearDxCall && value.clearDxGrid); QVERIFY(value.tuneWatchdog);
  }

  void boundedSpacingAndExistingSessionSettingsPreserved() {
    QTemporaryDir dir; QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
    settings.beginGroup("Configuration");
    settings.setValue("AlignSteps", -500); settings.setValue("AlignSteps2", 500);
    settings.setValue("TxWatchdogTimer", 999); settings.setValue("NTP/Enabled", true);
    settings.setValue("NTP/IntervalMinutes", 15); settings.setValue("AutoQSOLogging", true);
    WSJTXGeneralSettings value; value.read(settings); value.write(settings);
    QCOMPARE(value.alignSteps, -2); QCOMPARE(value.alignSteps2, 5);
    QCOMPARE(settings.value("TxWatchdogTimer").toInt(), 999);
    QVERIFY(settings.value("NTP/Enabled").toBool());
    QCOMPARE(settings.value("NTP/IntervalMinutes").toInt(), 15);
    QVERIFY(settings.value("AutoQSOLogging").toBool());
  }

  void commonGroupMigrationWinsOverLegacyMainWindow() {
    QTemporaryDir dir;QSettings settings(dir.filePath("settings.ini"),QSettings::IniFormat);
    settings.setValue("MainWindow/SingleShotQSO",false);
    settings.setValue("MainWindow/EraseWindowsAtBandChange",true);
    settings.setValue("Common/SingleShotQSO",true);
    settings.setValue("Common/EraseWindowsAtBandChange",false);
    WSJTXGeneralSettings value;value.readRoot(settings);
    QVERIFY(value.disableTxAfter73);QVERIFY(!value.eraseBandActivity);QVERIFY(settings.group().isEmpty());
    settings.setValue("Configuration/73TxDisable",false);
    settings.setValue("Configuration/erase_BandActivity",true);
    value.readRoot(settings);QVERIFY(!value.disableTxAfter73);QVERIFY(value.eraseBandActivity);
    settings.remove("Configuration");settings.remove("Common/SingleShotQSO");settings.setValue("Common/73TxDisable",true);
    value.readRoot(settings);QVERIFY(value.disableTxAfter73);
  }

  void longLabelsKeepFullAccessibleTextAndTooltips() {
    QWidget parent;auto outer=new QVBoxLayout(&parent);auto group=new QGroupBox(&parent);
    group->setObjectName("display_group_box");outer->addWidget(group);auto grid=new QGridLayout(group);
    QString full="A complete setting description that is much longer than a narrow settings column";
    auto box=new QCheckBox(full,group);box->setToolTip("Additional explanation.");grid->addWidget(box,0,0);
    WSJTXGeneralSettings::connectControls(&parent);parent.resize(300,100);parent.show();
    QCoreApplication::processEvents();QCoreApplication::processEvents();
    QVERIFY(box->text()!=full);QVERIFY(box->toolTip().contains(full));QVERIFY(box->toolTip().contains("Additional explanation."));
    QCOMPARE(box->accessibleName(),full);
    parent.resize(1800,100);QCoreApplication::processEvents();QCoreApplication::processEvents();
    QCOMPARE(box->text(),full);QCOMPARE(box->toolTip(),QString("Additional explanation."));
  }

  void completeUiAndDraftChanges() {
    QDialog dialog; Ui::configuration_dialog ui; ui.setupUi(&dialog);
    QVERIFY(!dialog.findChild<QWidget*>("CW_id_after_73_check_box"));
    QVERIFY(!dialog.findChild<QWidget*>("CW_id_interval_spin_box"));
    int controls = 0;
    for (auto name : {"display_group_box", "behaviour_group_box", "additional_features_box"}) {
      auto group = dialog.findChild<QGroupBox*>(name); QVERIFY(group);
      controls += group->findChildren<QCheckBox*>().size() + group->findChildren<QSpinBox*>().size()
        + group->findChildren<QPushButton*>().size();
    }
    QCOMPARE(controls, 46); // The 43 upstream controls plus three retained JTDX options.
    QCOMPARE(ui.tx_watchdog_spin_box->maximum(), 999);
    QCOMPARE(ui.tune_timer_spin_box->maximum(), 300);
    WSJTXGeneralSettings::connectControls(&dialog);
    WSJTXGeneralSettings current; current.fullDuplex = true; current.quickCall = false;
    current.initializeControls(&dialog);
    ui.insert_blank_check_box->setChecked(true);
    QVERIFY(ui.cb_detailed_blank_line->isEnabled());
    ui.DXCC_check_box->setChecked(false);
    QVERIFY(!ui.countryName_check_box->isEnabled()); QVERIFY(!ui.countryPrefix_check_box->isEnabled());
    ui.tune_watchdog_check_box->setChecked(false); QVERIFY(!ui.tune_timer_spin_box->isEnabled());
    ui.cb_Align->setChecked(false); QVERIFY(!ui.align_spin_box->isEnabled());
    ui.Map_Grid_to_State->setChecked(false); QVERIFY(!ui.Map_All_Messages->isEnabled());
    ui.quick_call_check_box->setChecked(true);
    QVERIFY(!current.quickCall); // Merely editing a dialog must not change live settings.
    current.initializeControls(&dialog); // Cancel/reset restores the draft.
    QVERIFY(!ui.quick_call_check_box->isChecked()); QVERIFY(ui.DXCC_check_box->isChecked());
    ui.quick_call_check_box->setChecked(true);
    ui.cbClearDXcall->setChecked(true); ui.cbClearDXgrid->setChecked(false);
    current.acceptControls(&dialog);
    QVERIFY(current.quickCall && current.clearDxCall && !current.clearDxGrid);
    QVERIFY(current.fullDuplex); // A menu-owned setting has no duplicate dialog control.
  }
};

QTEST_MAIN(GeneralSettingsTests)
#include "GeneralSettingsTests.moc"
