#include <QtTest>
#include <QComboBox>
#include "Bands.hpp"
#include "FrequencyList.hpp"
#include "LiveFrequencyValidator.hpp"

class LiveFrequencyValidatorTests : public QObject {
  Q_OBJECT
private slots:
  void unitsAndLocale();
  void mergingUpstreamDefaultsPreservesEditedRows();
};
void LiveFrequencyValidatorTests::unitsAndLocale()
{
  auto original=QLocale();
  for(auto locale:QList<QLocale>{QLocale::c(),QLocale(QLocale::French,QLocale::France)}) {
    QLocale::setDefault(locale);
    Bands bands;FrequencyList_v2 frequencies(&bands);frequencies.reset_to_defaults();
    QComboBox combo;combo.setEditable(true);Radio::Frequency nominal=14074000;
    auto decimal=locale.decimalPoint();
    for(bool bareKhz:{false,true}) {
      LiveFrequencyValidator validator(&combo,&bands,&frequencies,&nominal,bareKhz);
      quint64 result=0;int emissions=0;
      connect(&validator,&LiveFrequencyValidator::valid,[&](quint64 value){result=value;++emissions;});
      auto check=[&](QString input,quint64 expected){
        int position=input.size();QVERIFY(validator.validate(input,position)!=QValidator::Invalid);
        int before=emissions;validator.fixup(input);QCOMPARE(emissions,before+1);QCOMPARE(result,expected);
      };
      check("74",bareKhz?14074000:74000000);
      check(QString("74")+decimal+"500k",14074500);
      check(QString("14")+decimal+"074M",14074000);
      check(QString("14")+decimal+"074",bareKhz?14014074:14074000);
      QString band="20m";int before=emissions;validator.fixup(band);QCOMPARE(emissions,before+1);QCOMPARE(bands.find(result),QString("20m"));
      band="70CM";before=emissions;validator.fixup(band);QCOMPARE(emissions,before+1);QCOMPARE(bands.find(result),QString("70cm"));
    }
  }
  QLocale::setDefault(original);
}
void LiveFrequencyValidatorTests::mergingUpstreamDefaultsPreservesEditedRows()
{
  Bands bands;FrequencyList_v2 defaults(&bands),edited(&bands);defaults.reset_to_defaults();
  FrequencyList_v2::FrequencyItems additions;
  for(auto const& item:defaults.frequency_list())
    if(item.mode_==Modes::Q65 || item.mode_==Modes::MSK144)additions<<item;
  QVERIFY(!additions.isEmpty());
  auto changed=additions.first();changed.default_=!changed.default_;
  FrequencyList_v2::Item custom{144123456,Modes::Q65,IARURegions::ALL,false};
  edited.frequency_list({changed,custom});edited.frequency_list_merge(additions);
  auto merged=edited.frequency_list();QVERIFY(merged.contains(custom));
  int count=0;for(auto const& item:merged)if(item==changed){++count;QCOMPARE(item.default_,changed.default_);}
  QCOMPARE(count,1);int size=merged.size();edited.frequency_list_merge(additions);QCOMPARE(edited.frequency_list().size(),size);
}
QTEST_MAIN(LiveFrequencyValidatorTests)
#include "LiveFrequencyValidatorTests.moc"
