#include "Bands.hpp"

void RegressionTest::catBandChangeUpdatesSelectionAndWorkedColors()
{
  w->on_stopTxButton_clicked();w->setManagedMode(ManagedMode::Off);stopWaveAudio();
  QTRY_VERIFY_WITH_TIMEOUT(w->m_logReady && !w->m_logLoading && !w->m_logRefreshPending
    && !w->m_logImportActive && !w->m_logRefreshTimer.isActive(),20000);
  auto dialog=w->findChild<QDialog*>("configuration_dialog");QVERIFY(dialog);
  QMap<QString,bool> previous,selected;
  for(auto kind:{"CQZ","ITUZ","DXCC","Grid","Px","Call"})
    for(auto suffix:{"","Band","BandMode"}) {
      auto name=QString("new")+kind+suffix+"_check_box";
      auto box=dialog->findChild<QCheckBox*>(name);QVERIFY(box);
      previous[name]=box->isChecked();selected[name]=QString(kind)=="Call";
    }
  for(auto name:{"workedColor_check_box","workedStriked_check_box","workedDontShow_check_box"}) {
    auto box=dialog->findChild<QCheckBox*>(name);QVERIFY(box);
    previous[name]=box->isChecked();selected[name]=QString(name)!="workedDontShow_check_box";
  }
  auto configure=[&](QMap<QString,bool> values) {
    QTimer::singleShot(0,dialog,[dialog,values]{
      for(auto it=values.cbegin();it!=values.cend();++it)dialog->findChild<QCheckBox*>(it.key())->setChecked(it.value());
      QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
    });
    QCOMPARE(w->m_config.exec(),int(QDialog::Accepted));
    w->ui->decodedTextBrowser->setConfiguration(&w->m_config);
  };
  auto oldRig=w->m_rigState;auto oldOperating=w->m_upstreamState;
  auto oldNominal=w->m_freqNominal;auto oldTxNominal=w->m_freqTxNominal;
  auto oldErase=w->m_autoEraseBC;auto oldStart=w->m_start2;
  struct Restore {std::function<void()> action;~Restore(){action();}} restore{[&]{
    w->on_stopTxButton_clicked();w->m_rigState=oldRig;w->m_upstreamState=oldOperating;
    w->m_freqNominal=oldNominal;w->m_freqTxNominal=oldTxNominal;w->m_autoEraseBC=oldErase;
    w->m_start2=oldStart;w->m_bandEdited=false;configure(previous);w->displayDialFrequency();
  }};
  configure(selected);w->m_mode=w->m_modeTx="FT8";w->m_TRperiod=15;
  w->m_diskData=false;w->m_monitoroff=true;w->m_start2=false;w->m_autoEraseBC=true;w->m_bandEdited=false;
  w->m_upstreamState=UpstreamOperatingState{};
  w->m_upstreamState.expectReceiveFrequency(14074000,0);
  w->m_upstreamState.expectReceiveFrequency(7074000,0);
  w->m_freqNominal=w->m_freqTxNominal=7074000;
  w->m_rigState.online(true);w->m_rigState.ptt(false);w->m_rigState.split(false);w->m_rigState.frequency(7074000);
  w->m_lastBand="40m";w->ui->bandComboBox->setCurrentText("40m");
  w->m_logBook.init("N0CALL","","","ZZ9QA");
  w->m_logBook.addAsWorked("K7BND","40m","FT8","20261031","FN31","");
  auto display=[&](QString stamp) {
    w->ui->decodedTextBrowser->clear();
    DecodedText decoded(stamp+" -10  0.1 1700 ~ CQ K7BND FN31");
    w->ui->decodedTextBrowser->displayDecodedText(&decoded,"N0CALL","","",false,w->m_logBook,
      w->m_qsoHistory,w->m_qsoHistory,w->m_freqNominal,"FT8",false,true,1500,{},{},{},{},false,w.get());
    return w->ui->decodedTextBrowser->document()->find("K7BND");
  };
  auto on40=display("000015");QVERIFY(!on40.isNull());QVERIFY(on40.charFormat().fontStrikeOut());
  auto workedColor=on40.charFormat().background().color();
  auto state=w->m_rigState;state.frequency(14074000);w->handle_transceiver_update(state);
  QCOMPARE(w->m_freqNominal,quint64(14074000));QCOMPARE(w->ui->bandComboBox->currentText(),QString("20m"));
  QCOMPARE(w->m_lastBand,QString("20m"));QVERIFY(w->ui->decodedTextBrowser->toPlainText().isEmpty());
  auto on20=display("000045");QVERIFY(!on20.isNull());QVERIFY(!on20.charFormat().fontStrikeOut());
  QVERIFY(on20.charFormat().background().color()!=workedColor);
  // A stale model label must be repaired without clearing this band's data.
  auto contents=w->ui->decodedTextBrowser->toPlainText();
  w->ui->bandComboBox->setCurrentText("40m");w->displayDialFrequency();
  QCOMPARE(w->ui->bandComboBox->currentText(),QString("20m"));
  QCOMPARE(w->ui->decodedTextBrowser->toPlainText(),contents);
  // Preserve text being manually edited on the same band.
  w->m_bandEdited=true;w->ui->bandComboBox->setCurrentText("14.1");w->displayDialFrequency();
  QCOMPARE(w->ui->bandComboBox->currentText(),QString("14.1"));w->m_bandEdited=false;
  // A split TX VFO never changes the RX band used for highlighting.
  w->m_rigState.split(true);w->m_rigState.ptt(true);w->m_rigState.tx_frequency(7074000);w->displayDialFrequency();
  QCOMPARE(w->ui->bandComboBox->currentText(),QString("20m"));QCOMPARE(w->m_lastBand,QString("20m"));
  w->m_rigState.split(false);w->m_rigState.ptt(false);
  // Visit every ADIF band forward and backward, with each frequency already
  // in CAT command history. Only 40 m has a logged QSO with this station.
  auto bands=w->m_config.bands();int checked=0;
  for(int direction:{1,-1})for(int step=0;step<bands->rowCount();++step) {
    int row=direction>0?step:bands->rowCount()-1-step;
    auto band=bands->data(bands->index(row,0),Qt::EditRole).toString();
    auto low=bands->data(bands->index(row,1),Qt::EditRole).toULongLong();
    auto high=bands->data(bands->index(row,2),Qt::EditRole).toULongLong();
    auto frequency=low+(high-low)/2;
    QCOMPARE(ADIF::bandFromFrequency(low/1.e6),band);
    QCOMPARE(ADIF::bandFromFrequency(high/1.e6),band);
    w->m_upstreamState.expectReceiveFrequency(frequency,0);
    auto next=w->m_rigState;next.frequency(frequency);w->handle_transceiver_update(next);
    QCOMPARE(w->m_freqNominal,frequency);QCOMPARE(w->ui->bandComboBox->currentText(),band);
    QCOMPARE(w->m_lastBand,band);
    auto rendered=display("000115");QVERIFY(!rendered.isNull());
    QVERIFY2(rendered.charFormat().fontStrikeOut()==(band=="40m"),qPrintable(band));
    QVERIFY2((rendered.charFormat().background().color()==workedColor)==(band=="40m"),qPrintable(band));
    ++checked;
  }
  QCOMPARE(checked,2*bands->rowCount());
  w->m_logBook.addAsWorked("K7OLD","2200m","FT8","20261031","FN31","");
  QString country;bool worked=false,bandMode=false;
  w->m_logBook.matchCall("K7OLD",country,worked,bandMode,136500,"FT8");
  QVERIFY(worked && bandMode);
  OperatorLog::Record legacy{{"STATION_CALLSIGN","N0CALL"},{"CALL","K7OLD"},{"QSO_DATE","20261031"},
    {"TIME_ON","120000"},{"MODE","FT8"},{"BAND","2200m"}};
  auto canonical=legacy;canonical["BAND"]="2190m";
  QCOMPARE(OperatorLog::identity(legacy),OperatorLog::identity(canonical));
  QVERIFY(!w->m_enableTx);QVERIFY(!w->m_manualTxAuthorized);
}
