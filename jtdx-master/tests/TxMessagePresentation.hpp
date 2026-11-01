// Included after the RegressionTest declaration: real display/state tests.
#include <QColorDialog>

void RegressionTest::ownMessagesUseSharedTransmitColor()
{
  for (bool dark : {false,true}) {
    for (auto const& pane : {QString("decodedTextBrowser"),QString("decodedTextBrowser2")}) {
      for (auto const& mode : {QString("FT8"),QString("FT4"),QString("FT2"),QString("JT65"),QString("Q65"),QString("MSK144")}) {
        DisplayText view;view.setObjectName(pane);view.setConfiguration(&w->m_config);
        view.useDarkStyle_=dark;
        // Legacy text inversion, wanted/DX highlights and receive filters must
        // never recolor or turn an own decode into an incoming station.
        view.displayTxtColor_=true;view.showcq_=true;view.displayWorkedDontShow_=true;
        view.setDxHighlightTarget("K1ABC","AA00");
        auto selected=QColor("#7AD6CA");
        for(auto const& color : {selected,QColor("#245283")}) {
          view.ownTransmissionBackground_=color.name();
          QsoHistory received,other,sent;received.init();other.init();sent.init();
          received.owndata("NA","K","AA00",false);other.owndata("NA","K","AA00",false);
          auto formatAt=[&](int position) {
            QTextCursor cursor(view.document());cursor.setPosition(position);
            cursor.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor);
            return cursor.charFormat();
          };
          auto expectedForeground=qGray(color.rgb())<128 ? QColor("#ffffff") : QColor("#000000");
          view.clear();
          view.displayTransmittedText("K1ABC N0CALL -10","N0CALL","K1ABC","",mode,1500,color,sent);
          QVERIFY(view.toPlainText().contains("K1ABC N0CALL -10"));
          QCOMPARE(formatAt(0).background().color(),color);
          QCOMPARE(formatAt(view.toPlainText().indexOf("K1ABC")).foreground().color(),expectedForeground);
          for(auto const& message : {QString("CQ N0CALL AA00"),QString("CQ DX N0CALL AA00"),
              QString("K1ABC N0CALL -10"),QString("K1ABC <N0CALL> RR73"),
              QString("DE N0CALL 73"),QString("CQ N0CALL/P AA00")}) {
            view.clear();
            auto prefix=mode=="JT65" ? QString("1200 -10  0.1 1500 # ") : QString("120000 -10  0.1 1500 ~ ");
            DecodedText decoded(prefix+message,nullptr,mode=="Q65" || mode=="MSK144");
            auto visible=view.displayDecodedText(&decoded,"N0CALL","K1ABC","AA00",true,w->m_logBook,
              received,other,14074000,mode,false,false,1500,{"N0CALL"});
            QCOMPARE(visible,2);
            QVERIFY(view.toPlainText().contains(message));
            QCOMPARE(formatAt(0).background().color(),color);
            QCOMPARE(formatAt(prefix.size()).background().color(),color);
            QCOMPARE(formatAt(prefix.size()).foreground().color(),expectedForeground);
            QVERIFY(received.calls().isEmpty());QVERIFY(other.calls().isEmpty());
          }
          // A station calling us is a receive message, not our transmission.
          for(auto const& message : {QString("N0CALL K1ABC FN20"),QString("CQ N0CALL1 AA00")}) {
            view.clear();
            DecodedText decoded("120000 -10  0.1 1500 ~ "+message);
            view.displayDecodedText(&decoded,"N0CALL","K1ABC","FN20",true,w->m_logBook,
              received,other,14074000,"FT8",true,true,1500);
            QVERIFY(!view.toPlainText().isEmpty());
            QVERIFY(formatAt(0).background().color()!=color);
          }
          QVERIFY(received.calls().contains("K1ABC"));
        }
      }
    }
  }
}

void RegressionTest::ownTransmitColorIsConfigurable()
{
  auto dialog=w->findChild<QDialog*>("configuration_dialog");QVERIFY(dialog);
  auto button=dialog->findChild<QPushButton*>("pbTxMsg");QVERIFY(button);QVERIFY(button->isEnabled());
  auto original=w->m_config.color_TxMsg();
  QCOMPARE(original,QColor("#7AD6CA"));
  auto chooseColor=[&](QColor color) {
    QTimer::singleShot(0,dialog,[=] {
      QTimer::singleShot(0,dialog,[=] {
        auto chooser=qobject_cast<QColorDialog*>(QApplication::activeModalWidget());
        if(chooser){chooser->setCurrentColor(color);chooser->accept();}
      });
      button->click();
      QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
    });
    QCOMPARE(w->m_config.exec(),int(QDialog::Accepted));
  };
  QColor custom("#d79fe8");chooseColor(custom);
  QCOMPARE(w->m_config.color_TxMsg(),custom);
  DisplayText left,right;left.setConfiguration(&w->m_config);right.setConfiguration(&w->m_config);
  QsoHistory sent,received;sent.init();received.init();
  right.displayTransmittedText("CQ N0CALL AA00","N0CALL","","","FT8",1500,w->m_config.color_TxMsg(),sent);
  DecodedText own("120000 -10  0.1 1500 ~ CQ N0CALL AA00");
  left.displayDecodedText(&own,"N0CALL","","",true,w->m_logBook,received,received,14074000,"FT8");
  for(auto view:{&left,&right}) {
    QTextCursor cursor(view->document());cursor.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor);
    QCOMPARE(cursor.charFormat().background().color(),custom);
  }
  auto output=qEnvironmentVariable("JTDX_FM_QA_DIR");
  if(!output.isEmpty()) {
    QDir().mkpath(output);DisplayText preview;preview.resize(760,230);preview.setContentFont(w->font());
    preview.appendText("Default own TX / full-duplex decode (#7AD6CA)");
    preview.displayTransmittedText("CQ N0CALL AA00","N0CALL","","","FT8",1500,QColor("#7AD6CA"),sent);
    preview.displayDecodedText(&own,"N0CALL","","",true,w->m_logBook,received,received,14074000,"FT8");
    preview.appendText("Custom own TX / full-duplex decode (#D79FE8)");
    preview.displayTransmittedText("CQ N0CALL AA00","N0CALL","","","FT8",1500,custom,sent);
    preview.displayDecodedText(&own,"N0CALL","","",true,w->m_logBook,received,received,14074000,"FT8");
    preview.show();QApplication::processEvents();preview.grab().save(output+"/own-transmit-colors.png");
  }
  chooseColor(original);QCOMPARE(w->m_config.color_TxMsg(),original);
}

void RegressionTest::hiddenTransmitStillUpdatesQsoHistory()
{
  DisplayText view;QsoHistory history;history.init();
  view.displayTransmittedText("K1ABC N0CALL 73","N0CALL","K1ABC","","FT8",1500,QColor("#7AD6CA"),history,false);
  QVERIFY(view.toPlainText().isEmpty());
  QString grid;QCOMPARE(history.status("K1ABC",grid),QsoHistory::S73);
  QVERIFY(!view.wastx_);
}
