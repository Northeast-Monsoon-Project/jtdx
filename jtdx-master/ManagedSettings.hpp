#ifndef JTDXF_MANAGED_SETTINGS_HPP
#define JTDXF_MANAGED_SETTINGS_HPP
#include "ManagedMode.hpp"
#include <QWidget>
class QCheckBox;class QSpinBox;
class ManagedSettingsWidget : public QWidget {
  Q_OBJECT
public:
  explicit ManagedSettingsWidget(QWidget* parent=nullptr);
  void setSettings(ManagedSettings const&);
  ManagedSettings settings() const;
private:
  struct Controls {QHash<unsigned,QCheckBox*> criteria;QCheckBox* answer;QCheckBox* freeTxSlot;QSpinBox* limit;};
  Controls hunter_,enthusiast_;
};
#endif
