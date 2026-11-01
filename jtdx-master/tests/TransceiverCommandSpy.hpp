#ifndef JTDXF_TEST_TRANSCEIVER_COMMAND_SPY_HPP
#define JTDXF_TEST_TRANSCEIVER_COMMAND_SPY_HPP
#include <QMetaMethod>
#include <QSignalSpy>
#include "Transceiver.hpp"

// Configuration::transceiver_ptt is a slot. Observe the real command emitted
// to the backend instead; spying on that slot silently misses PTT requests.
class TransceiverCommandSpy : public QSignalSpy {
  static QObject* configuration(QObject* owner) {
    return owner?owner->findChild<QObject*>("configuration_dialog"):nullptr;
  }
  static QMetaMethod command(QObject* owner) {
    auto object=configuration(owner);
    if(object)for(int i=0;i<object->metaObject()->methodCount();++i) {
      auto method=object->metaObject()->method(i);
      if(method.methodType()==QMetaMethod::Signal && method.name()=="set_transceiver")return method;
    }
    return {};
  }
public:
  explicit TransceiverCommandSpy(QObject* owner):QSignalSpy(configuration(owner),command(owner)) {}
  int pttOnCount() const {
    int result=0;
    for(auto const& emission:*this)
      if(qvariant_cast<Transceiver::TransceiverState>(emission.at(0)).ptt())++result;
    return result;
  }
};
#endif
