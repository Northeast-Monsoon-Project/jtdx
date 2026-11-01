#include "Blacklist.hpp"
#include "StationProfiles.hpp"
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLockFile>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QVBoxLayout>
#include <algorithm>

namespace {
constexpr qint64 maxBytes=1024*1024;
constexpr int maxEntries=10000;
QByteArray digest(bool exists,QByteArray const& data) {
  return QCryptographicHash::hash((exists?QByteArray("1:"):QByteArray("0:"))+data,QCryptographicHash::Sha256);
}
bool read(QString const& path,QByteArray* bytes,bool* exists,QString* error) {
  QFile file(path);*exists=file.exists();bytes->clear();
  if(!*exists)return true;
  if(!file.open(QIODevice::ReadOnly)){*error=Blacklist::tr("Cannot open blacklist: %1").arg(file.errorString());return false;}
  if(file.size()>maxBytes){*error=Blacklist::tr("Blacklist file is too large.");return false;}
  *bytes=file.read(maxBytes+1);
  if(file.error()!=QFileDevice::NoError){*error=Blacklist::tr("Cannot open blacklist: %1").arg(file.errorString());return false;}
  if(bytes->size()>maxBytes){*error=Blacklist::tr("Blacklist file is too large.");return false;}
  return true;
}
}

Blacklist::Blacklist(QObject* parent):QObject(parent) {
  reloadTimer_.setSingleShot(true);reloadTimer_.setInterval(100);
  connect(&reloadTimer_,&QTimer::timeout,this,[this]{reload();});
  connect(&watcher_,&QFileSystemWatcher::fileChanged,this,[this]{reloadTimer_.start();});
  connect(&watcher_,&QFileSystemWatcher::directoryChanged,this,[this] {
    if(fileStamp()!=stamp_)reloadTimer_.start();
  });
}

QString Blacklist::matchingHelp() {
  return tr("A base callsign blocks its portable prefixes and suffixes (K1ABC also blocks EA8/K1ABC and K1ABC/P). A callsign containing / blocks only that exact callsign.");
}
QString Blacklist::validate(QList<Entry> const& entries) {
  if(entries.size()>maxEntries)return tr("Blacklist has too many entries.");
  QSet<QString> calls;
  for(auto const& entry:entries) {
    StationProfile profile;profile.call=StationProfiles::normalize(entry.call);profile.grid="AA00";
    if(!StationProfiles::validate(profile).isEmpty())return tr("Invalid callsign: %1").arg(entry.call);
    if(calls.contains(profile.call))return tr("Duplicate callsign: %1").arg(profile.call);
    if(entry.note.size()>512)return tr("Blacklist notes must not exceed 512 characters.");
    calls.insert(profile.call);
  }
  return {};
}
bool Blacklist::fail(QString const& message,QString* error) {
  bool notify=ready_ || error_!=message;ready_=false;error_=message;
  if(error)*error=message;
  if(notify){emit loadError(message);emit changed();}
  return false;
}
QString Blacklist::fileStamp() const {
  if(path_.isEmpty())return {};
  QFileInfo info(path_);
  return QString::number(info.exists())+':'+QString::number(info.size())+':'+QString::number(info.lastModified().toMSecsSinceEpoch());
}
void Blacklist::watch() {
  if(!watcher_.files().isEmpty())watcher_.removePaths(watcher_.files());
  if(!watcher_.directories().isEmpty())watcher_.removePaths(watcher_.directories());
  stamp_=fileStamp();
  if(path_.isEmpty())return;
  auto dir=QFileInfo(path_).dir();
  if(dir.exists())watcher_.addPath(dir.absolutePath());
  if(QFile::exists(path_))watcher_.addPath(path_);
}
void Blacklist::rebuild() {
  baseCalls_.clear();exactCalls_.clear();
  for(auto const& entry:entries_)
    (entry.call.contains('/')?exactCalls_:baseCalls_).insert(entry.call);
}
bool Blacklist::load(QString const& operatorCall,QDir const& operatorDirectory,QString* error) {
  reloadTimer_.stop();
  auto call=StationProfiles::normalize(operatorCall);
  auto path=call.isEmpty()?QString():operatorDirectory.absoluteFilePath("blacklist.json");
  if(call!=operatorCall_ || path!=path_) {
    entries_.clear();rebuild();fingerprint_.clear();ready_=false;error_.clear();
    operatorCall_=call;path_=path;
  }
  if(path_.isEmpty()) {watch();return fail(tr("Select an operator before editing the blacklist."),error);}
  if(!operatorDirectory.mkpath(".")) {watch();return fail(tr("Cannot create blacklist directory."),error);}
  return reload(error);
}
bool Blacklist::reload(QString* error) {
  if(error)error->clear();
  if(path_.isEmpty())return fail(tr("Select an operator before editing the blacklist."),error);
  QByteArray bytes;bool exists=false;QString problem;
  if(!read(path_,&bytes,&exists,&problem)){watch();return fail(problem,error);}
  auto fingerprint=digest(exists,bytes);
  if(ready_ && fingerprint==fingerprint_){watch();return true;}
  QList<Entry> entries;
  if(exists) {
    QJsonParseError parseError;auto document=QJsonDocument::fromJson(bytes,&parseError);
    auto invalid=tr("Invalid blacklist file; the original has been preserved.");
    if(parseError.error!=QJsonParseError::NoError || !document.isObject()) {watch();return fail(invalid,error);}
    auto object=document.object();
    if(object.value("format").toString()!="JTDX-F blacklist" || !object.value("entries").isArray()) {watch();return fail(invalid,error);}
    if(object.value("version").toDouble(-1)!=1) {watch();return fail(tr("Unsupported blacklist file version."),error);}
    for(auto const& value:object.value("entries").toArray()) {
      if(!value.isObject() || !value.toObject().value("call").isString() ||
         (!value.toObject().value("note").isUndefined() && !value.toObject().value("note").isString())) {watch();return fail(invalid,error);}
      auto entry=value.toObject();entries.append({StationProfiles::normalize(entry.value("call").toString()),entry.value("note").toString()});
    }
    problem=validate(entries);if(!problem.isEmpty()){watch();return fail(problem,error);}
  }
  std::sort(entries.begin(),entries.end(),[](Entry const& a,Entry const& b){return a.call<b.call;});
  entries_=entries;rebuild();fingerprint_=fingerprint;ready_=true;error_.clear();watch();emit changed();
  return true;
}
bool Blacklist::save(QList<Entry> const& entries,QString* error,QByteArray const& expectedRevision) {
  if(error)error->clear();
  auto reject=[&](QString const& message){if(error)*error=message;return false;};
  if(!ready_ || path_.isEmpty())return reject(tr("Blacklist is not ready. Repair the existing file before saving."));
  auto problem=validate(entries);if(!problem.isEmpty())return reject(problem);
  if(!expectedRevision.isEmpty() && expectedRevision!=contentRevision())
    return reject(tr("Blacklist changed in another program. Reload it before saving."));
  QLockFile lock(QFileInfo(path_).dir().filePath(".blacklist.lock"));
  if(!lock.tryLock())return reject(tr("Blacklist is busy. Please retry."));
  QByteArray prior;bool exists=false;
  if(!read(path_,&prior,&exists,&problem))return reject(problem);
  if(digest(exists,prior)!=fingerprint_)return reject(tr("Blacklist changed in another program. Reload it before saving."));
  QJsonArray array;
  for(auto const& entry:entries)array.append(QJsonObject{{"call",StationProfiles::normalize(entry.call)},{"note",entry.note.trimmed()}});
  auto bytes=QJsonDocument(QJsonObject{{"format","JTDX-F blacklist"},{"version",1},{"entries",array}}).toJson(QJsonDocument::Indented);
  if(bytes.size()>maxBytes)return reject(tr("Blacklist file is too large."));
  QSaveFile file(path_);
  if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit())
    return reject(tr("Cannot save blacklist: %1").arg(file.errorString()));
  return reload(error);
}
QByteArray Blacklist::contentRevision() const {
  auto call=operatorCall_.toUtf8(),path=path_.toUtf8();
  return QCryptographicHash::hash(QByteArray::number(call.size())+':'+call+
    QByteArray::number(path.size())+':'+path+fingerprint_,QCryptographicHash::Sha256);
}
bool Blacklist::matches(QString call) const {
  call=StationProfiles::normalize(call);
  if(call.startsWith('<') && call.endsWith('>'))call=call.mid(1,call.size()-2);
  if(exactCalls_.contains(call) || baseCalls_.contains(call))return true;
  // Only complete slash-separated components match: K1ABC covers
  // EA8/K1ABC/P, but cannot accidentally block K1ABC2.
  for(auto const& part:call.split('/'))if(baseCalls_.contains(part))return true;
  return false;
}

BlacklistDialog::BlacklistDialog(Blacklist* blacklist,QWidget* parent)
  :QDialog(parent),blacklist_(blacklist),entries_(blacklist->entries()),revision_(blacklist->contentRevision()) {
  setObjectName("blacklist_manager");setWindowTitle(tr("Permanent blacklist"));resize(560,390);
  auto layout=new QVBoxLayout(this);
  list_=new QListWidget(this);list_->setObjectName("blacklist_entries");list_->setToolTip(Blacklist::matchingHelp());layout->addWidget(list_);
  auto row=new QHBoxLayout;layout->addLayout(row);
  for(auto item:{qMakePair(tr("Add"),0),qMakePair(tr("Edit"),1),qMakePair(tr("Remove"),2)}) {
    auto button=new QPushButton(item.first,this);button->setObjectName(QString("blacklist_action_%1").arg(item.second));row->addWidget(button);
    button->setEnabled(blacklist_->ready());
    connect(button,&QPushButton::clicked,this,[this,item] {
      auto index=list_->currentRow();
      if(item.second==0)edit(-1);
      else if(index>=0 && item.second==1)edit(index);
      else if(index>=0){entries_.removeAt(index);refresh(qMin(index,entries_.size()-1));}
    });
  }
  connect(list_,&QListWidget::itemDoubleClicked,this,[this]{if(blacklist_->ready())edit(list_->currentRow());});
  errorLabel_=new QLabel(blacklist_->error(),this);errorLabel_->setWordWrap(true);errorLabel_->setVisible(!blacklist_->ready());layout->addWidget(errorLabel_);
  auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,this);layout->addWidget(buttons);
  buttons->button(QDialogButtonBox::Ok)->setText(tr("OK"));buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
  buttons->button(QDialogButtonBox::Ok)->setEnabled(blacklist_->ready());
  connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
  connect(buttons,&QDialogButtonBox::accepted,this,[this] {
    QString error;
    if(!blacklist_->save(entries_,&error,revision_)){errorLabel_->setText(error);errorLabel_->show();return;}
    accept();
  });
  refresh();
}
void BlacklistDialog::refresh(int selected) {
  list_->clear();for(auto const& entry:entries_) {
    auto item=new QListWidgetItem(entry.call+(entry.note.isEmpty()?QString():"   "+entry.note),list_);
    item->setToolTip(entry.note+"\n"+Blacklist::matchingHelp());
  }
  list_->setCurrentRow(selected);
}
void BlacklistDialog::edit(int index) {
  Blacklist::Entry entry;if(index>=0)entry=entries_.at(index);
  QDialog dialog(this);dialog.setObjectName("blacklist_editor");dialog.setWindowTitle(index<0?tr("Add callsign"):tr("Edit callsign"));
  auto form=new QFormLayout(&dialog);
  auto call=new QLineEdit(entry.call,&dialog);call->setObjectName("blacklist_call");call->setMaxLength(12);call->setToolTip(Blacklist::matchingHelp());
  auto note=new QLineEdit(entry.note,&dialog);note->setObjectName("blacklist_note");note->setMaxLength(512);
  form->addRow(tr("Callsign:"),call);form->addRow(tr("Note:"),note);
  auto buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,&dialog);form->addRow(buttons);
  buttons->button(QDialogButtonBox::Save)->setText(tr("Save"));buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
  connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
  connect(buttons,&QDialogButtonBox::accepted,&dialog,[&] {
    entry={StationProfiles::normalize(call->text()),note->text().trimmed()};auto proposed=entries_;
    if(index<0)proposed.append(entry);else proposed[index]=entry;
    auto error=Blacklist::validate(proposed);
    if(!error.isEmpty()){QMessageBox::warning(&dialog,tr("Invalid entry"),error);return;}
    dialog.accept();
  });
  if(dialog.exec()==QDialog::Accepted) {
    if(index<0){index=entries_.size();entries_.append(entry);}else entries_[index]=entry;
    refresh(index);
  }
}
