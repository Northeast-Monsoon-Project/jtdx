// SPDX-License-Identifier: GPL-3.0-or-later
// Doppler methods follow WSJT-X 3.0.2 widgets/astro.cpp (WSJT development team).
#ifndef FM_ASTRO_DIALOG_HPP
#define FM_ASTRO_DIALOG_HPP
#include <QDialog>
#include <QJsonObject>
class QSettings;class QCheckBox;class QComboBox;class QLabel;class QSpinBox;class QDoubleSpinBox;
class AstroDialog final:public QDialog {
  Q_OBJECT
public:
  struct Correction {qint64 rx=0,tx=0;};
  explicit AstroDialog(QSettings*,QWidget* parent=nullptr);
  bool tracking() const;
  bool dither() const;
  int rit() const;
  void setNominalFrequency(quint64);
  void showData(QJsonObject const&);
  void showError(QString const&);
  Correction correction(QJsonObject const&,bool echo,bool fixedTransmit) const;
  static Correction dopplerCorrection(int method,double self,double dx,qint64 shift,bool echo);
signals:
  void trackingChanged();
  void frequencyRequested(quint64 hz);
protected:
  void hideEvent(QHideEvent*) override;
private:
  void save();
  QSettings *settings_;
  QCheckBox *tracking_,*shiftEnabled_,*dither_;
  QComboBox *method_;
  QSpinBox *shift_,*rit_;
  QDoubleSpinBox *frequency_;
  QLabel *data_;
};
#endif
