#include "WSJTXGeneralSettings.hpp"

#include <QCheckBox>
#include <QEvent>
#include <QGridLayout>
#include <QKeySequence>
#include <QSettings>
#include <QSpinBox>
#include <QStyle>
#include <QTimer>
#include <QWidget>

namespace {
class GeneralOptionText : public QObject {
public:
  explicit GeneralOptionText(QCheckBox* box):QObject(box),box_(box),full_(box->text()),tooltip_(box->toolTip()),shortcut_(box->shortcut()) {
    plain_=full_;plain_.replace("&&",QString(QChar(0x1f)));plain_.remove('&');plain_.replace(QChar(0x1f),'&');
    box_->setAccessibleName(plain_);
    box_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
    box_->setMinimumWidth(0);
    box_->installEventFilter(this);
  }
protected:
  bool eventFilter(QObject*,QEvent* event) override {
    if(event->type()==QEvent::Resize || event->type()==QEvent::Show || event->type()==QEvent::FontChange) {
      if(!pending_) {pending_=true;QTimer::singleShot(0,this,[this]{pending_=false;fit();});}
    }
    return false;
  }
private:
  void fit() {
    int available=qMax(0,box_->width()-box_->style()->pixelMetric(QStyle::PM_IndicatorWidth,nullptr,box_)
                       -box_->style()->pixelMetric(QStyle::PM_CheckBoxLabelSpacing,nullptr,box_)-6);
    bool elided=box_->fontMetrics().horizontalAdvance(plain_)>available;
    QString shown=elided?box_->fontMetrics().elidedText(plain_,Qt::ElideRight,available):full_;
    if(shown!=box_->text()){box_->setText(shown);box_->setShortcut(shortcut_);}
    QString tip=tooltip_;
    if(elided) {
      if(tip.contains("<body>"))tip.replace("<body>","<body><p><b>"+plain_.toHtmlEscaped()+"</b></p>");
      else tip=plain_+(tip.isEmpty()?QString():"\n\n"+tip);
    }
    box_->setToolTip(tip);
  }
  QCheckBox* box_;
  QString full_,plain_,tooltip_;
  QKeySequence shortcut_;
  bool pending_=false;
};

struct BoolField {
  char const* key;
  char const* control;
  bool WSJTXGeneralSettings::*member;
};
BoolField const fields[] = {
  {"DXCCEntity", "DXCC_check_box", &WSJTXGeneralSettings::dxcc},
  {"highlight_DXcall", "cbHighlightDXcall", &WSJTXGeneralSettings::highlightDxCall},
  {"highlight_DXgrid", "cbHighlightDXgrid", &WSJTXGeneralSettings::highlightDxGrid},
  {"DecodesFromTop", "decodes_from_top_check_box", &WSJTXGeneralSettings::decodesFromTop},
  {"DetailedBlank", "cb_detailed_blank_line", &WSJTXGeneralSettings::detailedBlank},
  {"VHFUHF", "enable_VHF_features_check_box", &WSJTXGeneralSettings::enableVhf},
  {"AlternateBindings", "alternate_bindings_check_box", &WSJTXGeneralSettings::alternateBindings},
  // Do not inherit the old QuickCall preference: it could not authorize TX.
  {"DoubleClickEnableTx", "quick_call_check_box", &WSJTXGeneralSettings::quickCall},
  {"73TxDisable", "disable_TX_on_73_check_box", &WSJTXGeneralSettings::disableTxAfter73},
  {"AutoAstroWindow", "auto_astro_check_box", &WSJTXGeneralSettings::autoAstro},
  {"ForceCallFirst", "force_call_1st_check_box", &WSJTXGeneralSettings::forceCallFirst},
  {"RepeatTx", "repeat_Tx_check_box", &WSJTXGeneralSettings::repeatTx},
  {"kHzWithoutK", "kHz_without_k_check_box", &WSJTXGeneralSettings::khzWithoutK},
  {"SingleDecode", "single_decode_check_box", &WSJTXGeneralSettings::singleDecode},
  {"TuneWatchdog", "tune_watchdog_check_box", &WSJTXGeneralSettings::tuneWatchdog},
  {"ProgressBarRed", "progress_bar_check_box", &WSJTXGeneralSettings::progressBarRed},
  {"set_RXtoTX", "cbRxToTxAfterQSO", &WSJTXGeneralSettings::rxToTxAfterQso},
  {"showDistance", "cb_showDistance", &WSJTXGeneralSettings::showDistance},
  {"erase_BandActivity", "cbEraseBandActivity", &WSJTXGeneralSettings::eraseBandActivity},
  {"clear_DXgrid", "cbClearDXgrid", &WSJTXGeneralSettings::clearDxGrid},
  {"MapGridAllEntity", "Map_All_Messages", &WSJTXGeneralSettings::gridMapAll},
  {"MapGridEntity", "Map_Grid_to_State", &WSJTXGeneralSettings::gridMap},
  {"AlignDistanceAzimuth", "cb_Align", &WSJTXGeneralSettings::align},
  {"AlternateEraseButtonBehavior", "alternate_erase_button_check_box", &WSJTXGeneralSettings::alternateErase},
  {"clear_DXcall", "cbClearDXcall", &WSJTXGeneralSettings::clearDxCall},
  {"WaitFeaturesEnabled", "enable_Wait_features_check_box", &WSJTXGeneralSettings::waitFeatures},
  {"showAzimuth", "cb_showAzimuth", &WSJTXGeneralSettings::showAzimuth},
  {"TxWarningDisabled", "disable_button_coloring_check_box", &WSJTXGeneralSettings::lessButtonColoring},
  {"FullDuplexMode", "", &WSJTXGeneralSettings::fullDuplex},
  {"CheckSWR", "check_SWR_check_box", &WSJTXGeneralSettings::checkSwr}
};
}

void WSJTXGeneralSettings::readRoot(QSettings& settings)
{
  auto single=settings.value("Common/SingleShotQSO",settings.value("Common/73TxDisable",
    settings.value("MainWindow/SingleShotQSO",settings.value("MainWindow/73TxDisable"))));
  auto erase=settings.value("Common/EraseWindowsAtBandChange",settings.value("MainWindow/EraseWindowsAtBandChange"));
  auto duplex=settings.value("Common/FullDuplexMode",settings.value("MainWindow/FullDuplexMode"));
  settings.beginGroup("Configuration");read(settings,single,erase,duplex);settings.endGroup();
}

void WSJTXGeneralSettings::migrateDefaultsV361(QSettings& settings)
{
  settings.beginGroup("Configuration");
  if (!settings.value("QsoCleanupDefaultsV361", false).toBool()) {
    // These defaults also apply to existing installations, where V3.6.0
    // already saved the old false values. Do not reset later manual changes.
    for (auto key : {"set_RXtoTX", "erase_BandActivity", "clear_DXcall", "clear_DXgrid"})
      settings.setValue(key, true);
    settings.setValue("QsoCleanupDefaultsV361", true);
  }
  settings.endGroup();
}

void WSJTXGeneralSettings::read(QSettings& settings, QVariant const& legacySingleShot,
                              QVariant const& legacyErase, QVariant const& legacyFullDuplex)
{
  *this = WSJTXGeneralSettings();
  if (legacySingleShot.isValid()) disableTxAfter73 = legacySingleShot.toBool();
  if (legacyErase.isValid()) eraseBandActivity = legacyErase.toBool();
  if (legacyFullDuplex.isValid()) fullDuplex = legacyFullDuplex.toBool();
  clearDxCall = clearDxGrid = settings.value("ClearCallGrid", true).toBool();
  tuneWatchdog = settings.value("TuneTimer", 30).toInt() != 0;
  for (auto const& field : fields)
    this->*(field.member) = settings.value(QLatin1String(field.key), this->*(field.member)).toBool();
  alignSteps = qBound(-2, settings.value("AlignSteps", 2).toInt(), 10);
  alignSteps2 = qBound(-8, settings.value("AlignSteps2", 0).toInt(), 5);
}

void WSJTXGeneralSettings::write(QSettings& settings) const
{
  for (auto const& field : fields)
    settings.setValue(QLatin1String(field.key), this->*(field.member));
  settings.setValue("AlignSteps", qBound(-2, alignSteps, 10));
  settings.setValue("AlignSteps2", qBound(-8, alignSteps2, 5));
}

void WSJTXGeneralSettings::initializeControls(QWidget* parent) const
{
  for (auto const& field : fields)
    if (*field.control)
      if (auto box = parent->findChild<QCheckBox*>(QLatin1String(field.control)))
        box->setChecked(this->*(field.member));
  if (auto spin = parent->findChild<QSpinBox*>("align_spin_box")) spin->setValue(alignSteps);
  if (auto spin = parent->findChild<QSpinBox*>("align_spin_box2")) spin->setValue(alignSteps2);
}

void WSJTXGeneralSettings::acceptControls(QWidget* parent)
{
  for (auto const& field : fields)
    if (*field.control)
      if (auto box = parent->findChild<QCheckBox*>(QLatin1String(field.control)))
        this->*(field.member) = box->isChecked();
  if (auto spin = parent->findChild<QSpinBox*>("align_spin_box")) alignSteps = qBound(-2, spin->value(), 10);
  if (auto spin = parent->findChild<QSpinBox*>("align_spin_box2")) alignSteps2 = qBound(-8, spin->value(), 5);
}

void WSJTXGeneralSettings::connectControls(QWidget* parent)
{
  for(auto groupName:{"display_group_box","behaviour_group_box","additional_features_box"})
    if(auto group=parent->findChild<QWidget*>(groupName)) {
      if(auto grid=qobject_cast<QGridLayout*>(group->layout())) {grid->setColumnStretch(0,1);grid->setColumnStretch(1,1);}
      for(auto box:group->findChildren<QCheckBox*>())new GeneralOptionText(box);
    }
  auto bind = [parent](char const* source, char const* target) {
    auto box = parent->findChild<QCheckBox*>(QLatin1String(source));
    auto dependent = parent->findChild<QWidget*>(QLatin1String(target));
    if (!box || !dependent) return;
    QObject::connect(box, &QCheckBox::toggled, dependent, &QWidget::setEnabled);
    dependent->setEnabled(box->isChecked());
  };
  bind("insert_blank_check_box", "cb_detailed_blank_line");
  bind("DXCC_check_box", "countryName_check_box");
  bind("DXCC_check_box", "countryPrefix_check_box");
  bind("Map_Grid_to_State", "Map_All_Messages");
  bind("cb_Align", "align_spin_box");
  bind("cb_Align", "align_spin_box2");
  bind("tune_watchdog_check_box", "tune_timer_spin_box");
}
