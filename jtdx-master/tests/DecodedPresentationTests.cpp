#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include "DecodedPresentation.hpp"

class DecodedPresentationTests : public QObject {
  Q_OBJECT
private slots:
  void coordinatesAndUnits();
  void exactTokens();
  void columnsAndUtc();
  void gridTableValidation();
};

void DecodedPresentationTests::coordinatesAndUnits()
{
  double lat=0,lon=0;
  QVERIFY(DecodedPresentation::gridCoordinates("aa00",lat,lon));
  QCOMPARE(lat,-89.5);QCOMPARE(lon,-179.0);
  QVERIFY(DecodedPresentation::gridCoordinates("RR99xx99xx",lat,lon));
  QVERIFY(lat<90. && lat>89.99);QVERIFY(lon<180. && lon>179.99);
  for(auto const& bad:QStringList{"", "RR73", "AA0", "SS00", "AA00YY", "AA00AA0", "AA00AA00YY"})
    QVERIFY(!DecodedPresentation::gridCoordinates(bad,lat,lon));
  QCOMPARE(DecodedPresentation::distanceAzimuth("JJ00","JJ00",true,true,false),QString::fromUtf8("0 km / 0°"));
  auto km=DecodedPresentation::distanceAzimuth("JJ00","JJ10",true,false,false).split(' ').first().toInt();
  auto mi=DecodedPresentation::distanceAzimuth("JJ00","JJ10",true,false,true).split(' ').first().toInt();
  QVERIFY(km>220 && km<224);QVERIFY(mi>136 && mi<140);
  QCOMPARE(DecodedPresentation::distanceAzimuth("JJ00","JJ01",false,true,false),QString::fromUtf8("0°"));
  QVERIFY(DecodedPresentation::distanceAzimuth("JJ00","RR73",true,true,false).isEmpty());
}

void DecodedPresentationTests::exactTokens()
{
  QString text="K1ABC K1ABCD <EA8/K1ABC/P> K1ABC/P <K1ABC> FN20 FN20aa RR73";
  auto calls=DecodedPresentation::tokenRanges(text,"K1ABC");QCOMPARE(calls.size(),2);
  QCOMPARE(text.mid(calls[0].first,calls[0].second),QString("K1ABC"));
  QCOMPARE(DecodedPresentation::tokenRanges(text,"EA8/K1ABC/P").size(),1);
  QCOMPARE(DecodedPresentation::tokenRanges(text,"K1ABC/P").size(),1);
  QCOMPARE(DecodedPresentation::tokenRanges(text,"FN20bb",true).size(),2);
  QVERIFY(DecodedPresentation::tokenRanges(text,"RR73",true).isEmpty());
}

void DecodedPresentationTests::columnsAndUtc()
{
  auto row=QString(49,'x')+DecodedPresentation::appendage(49,"United States","NJ-PA","123 km",true,2,0);
  QCOMPARE(row.indexOf("United States"),52);QCOMPARE(row.indexOf('['),72);
  QCOMPARE(row.indexOf(QChar::Nbsp),49);
  auto country=QString(100,'c');
  row=DecodedPresentation::appendage(49,country,"NJ","123 km",true,-2,-8);
  QVERIFY(row.contains(country));QVERIFY(row.contains(" NJ [123 km]"));
  QDateTime local(QDate(2026,10,30),QTime(8,0),Qt::OffsetFromUTC,8*3600);
  auto separator=DecodedPresentation::periodSeparator(local,"20m","FT8",true);
  QVERIFY(separator.contains("2026-10-30 00:00:00 UTC"));
  QVERIFY(separator.contains("20m - FT8"));
  QVERIFY(!DecodedPresentation::periodSeparator(local,"20m","FT8",false).contains("2026"));
}

void DecodedPresentationTests::gridTableValidation()
{
  QTemporaryDir temp;QVERIFY(temp.isValid());
  QString path=temp.filePath("grid.dat");GridStateMap table;
  auto write=[&](QByteArray const& data){QFile f(path);QVERIFY(f.open(QIODevice::WriteOnly));QCOMPARE(f.write(data),qint64(data.size()));};
  write("FN<\n20:NJ-PA,\n21:NY>\nRR<\n99:AK>\n");
  QVERIFY(table.load(path));QCOMPARE(table.size(),3);QCOMPARE(table.state("fn20aa"),QString("NJ-PA"));
  QCOMPARE(table.state("RR99"),QString("AK"));QVERIFY(table.state("RR73").isEmpty());
  for(auto const& data:QList<QByteArray>{"FN<\n20:NJ,\n", "SS<\n99:AK>\n", "FN<\n100:NY>\n", "FN<\n20:NJ,\n20:PA>\n", "FN<\nXX:NJ>\n"}) {
    write(data);QVERIFY(!table.load(path));QCOMPARE(table.size(),0);
  }
}

QTEST_GUILESS_MAIN(DecodedPresentationTests)
#include "DecodedPresentationTests.moc"
