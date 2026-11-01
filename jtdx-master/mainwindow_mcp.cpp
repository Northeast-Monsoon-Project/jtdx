#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "McpServer.hpp"
#include "FrequencyList.hpp"
#include "Bands.hpp"
#include "widegraph.h"
#include <QJsonDocument>
#include <QFile>
#include <QDateTime>
#include <QApplication>
extern int g_iptt;

void MainWindow::configureMcp()
{
  if (!m_mcp) {
    m_mcp = new McpServer([this](QString const& name, QJsonObject const& args) { return mcpCall(name, args); }, this);
    m_mcpLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_mcpLabel);
  }
  auto settings = m_config.mcp_settings();
  QString error = m_mcp->configure(settings);
  m_mcpLabel->setText(m_mcp->listening() ? tr("MCP %1").arg(settings.port) : settings.enabled ? tr("MCP error") : tr("MCP off"));
  m_mcpLabel->setToolTip(error.isEmpty() ? tr("MCP settings: File > Settings > MCP") : error);
  if (!error.isEmpty()) statusBar()->showMessage(tr("MCP: %1").arg(error), 15000);
}

void MainWindow::mcpEvent(QString const& type, QJsonObject data)
{
  data.insert("id", ++m_mcpEventId);
  data.insert("type", type);
  data.insert("utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
  m_mcpEvents.append(data);
  if (m_mcpEvents.size() > 500) m_mcpEvents.removeFirst();
}

QJsonObject MainWindow::mcpStatus() const
{
  return {{"application", "JTDX-F"}, {"version", QCoreApplication::applicationVersion()}, {"team", "Fluxmancer"},
    {"utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}, {"mode", m_mode},
    {"dial_hz", static_cast<double>(m_freqNominal)}, {"rx_hz", ui->RxFreqSpinBox->value()}, {"tx_hz", ui->TxFreqSpinBox->value()},
    {"callsign", m_config.my_callsign()}, {"grid", m_config.my_grid()}, {"dx_call", m_hisCall}, {"dx_grid", m_hisGrid},
    {"monitoring", m_monitoring}, {"tx_enabled", m_enableTx}, {"manual_tx_authorized",m_manualTxAuthorized}, {"superfox",m_superFoxMode}, {"transmitting", m_transmitting}, {"tuning", m_tune},
    {"decoder_busy", m_decoderBusy}, {"rig_online", m_rigOk}, {"tx_first", m_txFirst},
    {"managed_mode",QStringList{"off","dxcc_hunter","qso_enthusiast"}.at(int(m_managed.mode()))}, {"managed_tx_ready",m_managedTxReady},
    {"auto_sequence", m_autoseq}, {"single_qso", m_singleshot}, {"qso_state", static_cast<int>(m_status)},
    {"watchdog_minutes", m_config.watchdog()}, {"watchdog_expired", m_txwatchdog},
    {"watchdog_finishing_qso",m_watchdogFinishing},
    {"watchdog_remaining_seconds", m_txwatchdog ? 0 : m_txSession.remainingSeconds(m_config.watchdog())},
    {"last_logged_call", m_lastloggedcall}, {"control_allowed", m_config.mcp_settings().allowControl},
    {"transmit_allowed", m_config.mcp_settings().allowControl && m_config.mcp_settings().allowTransmit}};
}

QJsonObject MainWindow::mcpCall(QString const& name, QJsonObject const& a)
{
  auto fail = [](QString const& why) { return McpServer::failure(why); };
  if (name == "get_status") return McpServer::success(mcpStatus());
  if (name == "get_config") return McpServer::success({
    {"callsign", m_config.my_callsign()}, {"grid", m_config.my_grid()}, {"rig", m_config.rig_name()},
    {"auto_sequence", m_autoseq}, {"auto_tx", m_autoTx}, {"single_qso", m_singleshot},
    {"answer_worked", m_answerWorkedB4}, {"call_worked", m_callWorkedB4}, {"search_cq", m_callPrioCQ},
    {"selection", QStringList{"manual", "first", "middle", "end"}.value(m_callMode)},
    {"watchdog_minutes", m_config.watchdog()}, {"autolog", m_config.autolog()}, {"language", m_lang},
    {"data_directory", m_dataDir.absolutePath()}, {"log_directory", m_config.log_directory().absolutePath()}, {"log_files", QJsonArray::fromStringList(OperatorLog::files(m_config.my_callsign()))}, {"compatibility_log",OperatorLog::compatibilityFile(m_config.my_callsign())}, {"mcp_port", m_config.mcp_settings().port}});
  if (name == "get_decodes" || name == "get_events") {
    QJsonArray out; auto const& items = name == "get_decodes" ? m_mcpDecodes : m_mcpEvents;
    int after = a.value("after_id").toInt(0), limit = a.value("limit").toInt(100);
    for (auto const& v : items) if (v.toObject().value("id").toInt() > after && out.size() < limit) out.append(v);
    return McpServer::success({{"items", out}, {"latest_id", name == "get_decodes" ? m_mcpDecodeId : m_mcpEventId},
      {"oldest_id", items.isEmpty() ? 0 : items.first().toObject().value("id").toInt()},
      {"next_after_id", out.isEmpty() ? after : out.last().toObject().value("id").toInt()}});
  }
  if (name == "get_frequencies") {
    QJsonArray items;
    for (auto const& item : *m_config.frequencies()) items.append(QJsonObject{{"hz", static_cast<double>(item.frequency_)}, {"band", m_config.bands()->find(item.frequency_)}, {"mode", Modes::name(item.mode_)}});
    return McpServer::success({{"items", items}});
  }
  if (name == "get_messages" && m_mode=="JTTY")return McpServer::success({{"mode","JTTY"},{"text",m_jttyText->text()},{"encoding",m_jttyPreparing}});
  if (name == "get_messages") return McpServer::success({{"messages", QJsonArray{ui->tx1->text(), ui->tx2->text(), ui->tx3->text(), ui->tx4->text(), ui->tx5->currentText(), ui->tx6->text()}}, {"selected", m_ntx}, {"last_sent", m_lastMessageSent}});
  if (name == "get_log") {
    auto paths=OperatorLog::files(m_config.my_callsign());
    QString selected=a.value("file").toString();
    QString path=paths.isEmpty()?QString():paths.last();
    if(!selected.isEmpty()) {
      path.clear();for(auto const& candidate:paths) if(QFileInfo(candidate).fileName()==selected) {path=candidate;break;}
      if(path.isEmpty()) return fail("Select a basename from the active operator log_files list");
    }
    QFile file(path);
    if (!file.exists()) return McpServer::success({{"adif", ""}, {"truncated", false}});
    if (!file.open(QIODevice::ReadOnly)) return fail(file.errorString());
    qint64 start = qMax(0LL, file.size() - a.value("max_bytes").toInt(65536));
    file.seek(start);
    return McpServer::success({{"file",QFileInfo(file).fileName()}, {"adif", QString::fromUtf8(file.read(262144))}, {"truncated", start > 0}, {"offset", static_cast<double>(start)}});
  }
  // Stop remains available during dialogs and without control/TX permission.
  if (name == "stop_tx" || (name == "tune" && !a.value("enabled").toBool())) {
    TxAgainTimer.stop(); tuneButtonTimer.stop(); enableTxButtonTimer.stop();
    m_tuneup = false;
    haltTx("MCP stop request ");
    mcpEvent("control", {{"tool", name}});
    return McpServer::success(mcpStatus());
  }
  if (QApplication::activeModalWidget()) return fail("Close the active dialog before changing radio state");
  bool txActive = m_enableTx || m_transmitting || m_tune || g_iptt != 0;
  if (name != "start_tx" && name != "set_jtty_text" && name != "set_monitor" && name != "log_qso" && txActive)
    return fail("Stop TX before changing mode, frequency, target or options");
  if (name == "start_tx" || name == "tune") {
    if (!m_manualTxAuthorized) return fail("TX is locked. The operator must manually press Enable Tx in the application. Stop revokes this authorization.");
    if(name=="tune" && (m_mode=="JTTY" || m_mode=="Echo"))return fail("Tune is unavailable in this mode");
    if (m_config.my_callsign().isEmpty() || m_config.my_grid().size() < 4) return fail("Set callsign and grid in Settings first");
    if (m_diskData) return fail("TX is unavailable during file decoding");
    if (name == "start_tx") {
      if (m_tune) return fail("Stop Tune first");
      if (!m_enableTx) enableTx_mode(true); // repeat calls cannot extend the session
      if(m_mode=="JTTY") {
        if(m_transmitting || g_iptt || m_jttyPreparing)return fail("JTTY transmission is already pending");
        if(m_jttyText->text().trimmed().isEmpty())return fail("Prepare JTTY text first");
        requestJttyTransmit();
        if(!m_jttyPreparing)return fail("JTTY transmission was not accepted (check watchdog and audio devices)");
      }
    } else {
      if (m_enableTx || m_transmitting) return fail("Stop TX before starting Tune");
      if (!m_tune) { ui->tuneButton->setChecked(true); on_tuneButton_clicked(true); }
    }
  } else if(name=="set_jtty_text") {
    if(m_mode!="JTTY")return fail("Select JTTY mode first");
    if(m_transmitting || g_iptt || m_jttyPreparing)return fail("Wait for the current JTTY message to finish");
    m_jttyText->setText(a.value("text").toString());
  } else if (name == "set_monitor") {
    if (txActive && !a.value("enabled").toBool()) return fail("Stop TX before disabling reception");
    ui->monitorButton->setChecked(a.value("enabled").toBool());
    on_monitorButton_clicked(a.value("enabled").toBool());
  } else if (name == "set_mode") {
    QString mode = a.value("mode").toString();
    if (mode == "FT8") on_actionFT8_triggered();
    else if (mode == "FT4") on_actionFT4_triggered();
    else if (mode == "FT2") on_actionFT2_triggered();
    else if (mode == "JTTY") on_actionJTTY_triggered();
    else if (mode == "Q65" || mode == "Echo" || mode == "MSK144") selectUpstreamMode(mode);
    else if (mode == "JT65") on_actionJT65_triggered();
    else if (mode == "JT9") on_actionJT9_triggered();
    else if (mode == "T10") on_actionT10_triggered();
    else on_actionWSPR_2_triggered();
  } else if (name == "set_frequency") {
    Frequency hz = static_cast<Frequency>(a.value("hz").toDouble());
    if (m_config.bands()->find(hz).isEmpty()) return fail("Frequency is outside configured amateur bands");
    m_bandEdited = true; band_changed(hz); m_wideGraph->setRxBand(m_config.bands()->find(hz));
  } else if (name == "set_audio_frequency") {
    if (a.isEmpty()) return fail("Specify rx_hz or tx_hz");
    for (auto key : {"rx_hz", "tx_hz"}) if (a.contains(key)) {
      auto box = QString(key) == "rx_hz" ? ui->RxFreqSpinBox : ui->TxFreqSpinBox;
      if (!box->isEnabled() || a.value(key).toInt() < box->minimum() || a.value(key).toInt() > box->maximum()) return fail("Audio frequency is unavailable in this mode");
    }
    if (a.contains("rx_hz")) ui->RxFreqSpinBox->setValue(a.value("rx_hz").toInt());
    if (a.contains("tx_hz")) ui->TxFreqSpinBox->setValue(a.value("tx_hz").toInt());
  } else if (name == "set_tx_period") {
    if(m_mode=="JTTY")return fail("JTTY transmissions are asynchronous");
    if (!ui->TxMinuteButton->isEnabled()) return fail("TX period is unavailable in this mode");
    if (m_txFirst != a.value("first").toBool()) ui->TxMinuteButton->click();
  } else if (name == "set_target") {
    QString call = a.value("callsign").toString().trimmed().toUpper(), grid = a.value("grid").toString().toUpper();
    if(permanentlyBlocked(call))return fail("Station is blocked or the permanent blacklist is unavailable");
    if (!QRegularExpression("^(?=.{3,12}$)(?=.*[A-Z])(?=.*[0-9])[A-Z0-9]+(?:/[A-Z0-9]+)*$").match(call).hasMatch()) return fail("Invalid callsign");
    if (!grid.isEmpty() && !QRegularExpression("^[A-R]{2}[0-9]{2}([A-X]{2})?$").match(grid).hasMatch()) return fail("Invalid Maidenhead grid");
    clearDX(""); ui->dxCallEntry->setText(call); ui->dxGridEntry->setText(grid);
    ui->rptSpinBox->setValue(a.value("report").toInt(-10));
    genStdMsgs(QString::number(ui->rptSpinBox->value()));
    m_dateTimeQSOOn = m_jtdxtime->currentDateTimeUtc2();
  } else if (name == "reply_to_decode") {
    if(m_mode=="JTTY")return fail("Use set_target and set_jtty_text for JTTY");
    QJsonObject found;
    for (auto const& v : m_mcpDecodes) if (v.toObject().value("id") == a.value("id")) found = v.toObject();
    if (found.isEmpty()) return fail("Decode ID has expired");
    if (found.value("disk").toBool() || found.value("mode") != m_mode || found.value("dial_hz").toDouble() != static_cast<double>(m_freqNominal)
        || QDateTime::fromString(found.value("utc").toString(), Qt::ISODateWithMs).msecsTo(QDateTime::currentDateTimeUtc()) > 120000)
      return fail("Decode is stale, from a file, or from another band/mode");
    bool autoTx = m_autoTx; m_autoTx = false;
    QString line = found.value("raw").toString();
    processMessage(line, line.size(), false, false);
    m_autoTx = autoTx;
  } else if (name == "select_message") {
    if (m_mode.startsWith("WSPR") || m_mode=="JTTY" || m_mode=="Echo") return fail("Standard QSO messages are unavailable in this mode");
    switch (a.value("index").toInt()) {
      case 1: on_txb1_clicked(); break; case 2: on_txb2_clicked(); break; case 3: on_txb3_clicked(); break;
      case 4: on_txb4_clicked(); break; case 5: on_txb5_clicked(); break; case 6: on_txb6_clicked(); break;
    }
  } else if (name == "clear_target") clearDX(" cleared by MCP");
  else if (name == "set_options") {
    if (a.isEmpty()) return fail("Specify at least one option");
    if(a.contains("managed_mode")) {
      auto mode=a.value("managed_mode").toString();
      if(mode!="off" && (m_mode.startsWith("WSPR") || m_mode=="JTTY" || m_mode=="Echo"))return fail("Managed mode is unavailable in this mode");
      setManagedMode(mode=="dxcc_hunter"?ManagedMode::DxccHunter:mode=="qso_enthusiast"?ManagedMode::QsoEnthusiast:ManagedMode::Off);
    }
    if (a.contains("selection")) {
      QString selection = a.value("selection").toString();
      (selection == "manual" ? ui->actionCallNone : selection == "first" ? ui->actionCallFirst : selection == "middle" ? ui->actionCallMid : ui->actionCallEnd)->setChecked(true);
    }
    if (a.contains("auto_sequence")) { ui->AutoSeqButton->setChecked(a.value("auto_sequence").toBool()); on_AutoSeqButton_clicked(ui->AutoSeqButton->isChecked()); }
    if (a.contains("auto_tx")) { ui->AutoTxButton->setChecked(a.value("auto_tx").toBool()); on_AutoTxButton_clicked(ui->AutoTxButton->isChecked()); }
    if (a.contains("single_qso")) ui->actionSingleShot->setChecked(a.value("single_qso").toBool());
    if (a.contains("answer_worked")) ui->actionAnswerWorkedB4->setChecked(a.value("answer_worked").toBool());
    if (a.contains("call_worked")) ui->actionCallWorkedB4->setChecked(a.value("call_worked").toBool());
    if (a.contains("search_cq")) {
      if (a.value("search_cq").toBool() && m_callMode < 2) ui->actionCallMid->setChecked(true);
      ui->actionCallPriorityAndSearchCQ->setChecked(a.value("search_cq").toBool());
    }
    if (a.contains("watchdog_minutes")) m_config.set_watchdog(a.value("watchdog_minutes").toInt());
    writeSettings();
  } else if (name == "log_qso") {
    if (m_hisCall.isEmpty()) return fail("No current QSO to log");
    QTimer::singleShot(0, this, &MainWindow::on_logQSOButton_clicked);
  } else return fail("Tool is not implemented");
  mcpEvent("control", {{"tool", name}, {"arguments", a}});
  statusUpdate();
  return McpServer::success(mcpStatus());
}
