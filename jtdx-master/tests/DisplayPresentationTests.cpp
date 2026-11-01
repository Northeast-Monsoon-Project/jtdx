#include <QtTest>
#include <QScrollBar>
#include <QTextBlock>
#include "displaytext.h"

class DisplayPresentationTests : public QObject {
  Q_OBJECT
private slots:
  void periodStartsAtTop();
  void trimmingKeepsHighlightCursor();
  void extendedMessagesPreserveEntirePayload();
};

void DisplayPresentationTests::periodStartsAtTop()
{
  DisplayText view;view.resize(800,300);view.show();
  view.decodesFromTop_=true;view.updatePeriodMargin();
  for(int i=0;i<30;++i)view.appendText(QString("old row %1").arg(i));
  view.new_period();view.appendText("new period first decode");
  auto first=view.lastAppendedLine_;
  QCoreApplication::processEvents();
  QVERIFY2(qAbs(view.cursorRect(first).top()-int(view.document()->documentMargin()))<3,"First decode must be anchored at the top");
  int scroll=view.verticalScrollBar()->value();
  for(int i=0;i<30;++i)view.appendText(QString("new row %1").arg(i));
  QCOMPARE(view.verticalScrollBar()->value(),scroll);
  view.new_period();view.appendText("next period");
  QCoreApplication::processEvents();
  QVERIFY(qAbs(view.cursorRect(view.lastAppendedLine_).top()-int(view.document()->documentMargin()))<3);
  view.decodesFromTop_=false;view.updatePeriodMargin();view.appendText("normal scrolling restored");
  QVERIFY(view.cursorRect(view.lastAppendedLine_).top()<view.viewport()->height());
}

void DisplayPresentationTests::extendedMessagesPreserveEntirePayload()
{
  DisplayText view;
  QString payload="EA8/K1ABC/P W9XYZ R FN20AA EXTRA12345";
  QCOMPARE(payload.size(),37);
  QString row="123000 -10  0.1 1234 : "+payload;
  QCOMPARE(row.size(),60);
  auto labels=DecodedPresentation::appendage(61,"United States","NJ","123 km",true,2,0);
  view.appendText(row,"#ffffff","#000000",0," ","#000000",labels,false,true);
  auto rendered=view.toPlainText();
  QCOMPARE(rendered.left(60),row);QVERIFY(rendered.contains("EXTRA12345"));
  QVERIFY(view.document()->toRawText().indexOf(QChar::Nbsp)>=60);
  QTextCursor cursor(view.document());cursor.setPosition(58);cursor.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor);
  QVERIFY(cursor.charFormat().fontStrikeOut());
}

void DisplayPresentationTests::trimmingKeepsHighlightCursor()
{
  DisplayText view;view.document()->setMaximumBlockCount(3);
  view.setDxHighlightTarget("K1ABC","FN20");
  for(int i=0;i<5;++i)view.appendText("an old row");
  QString row="123000 -10  0.1 1234 ~ K1ABC K1ABCD FN20";
  view.appendText(row,"#ffffff","#000000",0," ","#000000","",false,true);
  view.highlightLastMessage(row.size(),"","");
  QCOMPARE(view.document()->blockCount(),3);
  auto first=view.lastAppendedLine_;
  QCOMPARE(first.position(),view.document()->lastBlock().position());
  QVERIFY2(view.document()->lastBlock().text().startsWith(row),qPrintable(view.document()->lastBlock().text()));
  auto formatAt=[&](int position){QTextCursor c=first;c.setPosition(first.position()+position);c.movePosition(QTextCursor::NextCharacter,QTextCursor::KeepAnchor);return c.charFormat();};
  auto call=formatAt(row.indexOf("K1ABC"));
  QCOMPARE(call.foreground().color(),QColor("#cc0000"));QVERIFY(call.fontStrikeOut());
  QCOMPARE(formatAt(row.indexOf("K1ABCD")).foreground().color(),QColor("#000000"));
  QCOMPARE(formatAt(row.indexOf("FN20")).foreground().color(),QColor("#0000c8"));
}

QTEST_MAIN(DisplayPresentationTests)
#include "DisplayPresentationTests.moc"
