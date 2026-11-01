#include "McpServer.hpp"
#include <QTcpSocket>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QCoreApplication>
#include <QRegularExpression>
#include <cmath>
#include <stdexcept>

namespace {
QJsonObject boolean() { return {{"type", "boolean"}}; }
QJsonObject integer(int low, int high) { return {{"type", "integer"}, {"minimum", low}, {"maximum", high}}; }
QJsonObject string(int max = 128) { return {{"type", "string"}, {"minLength", 1}, {"maxLength", max}}; }
QJsonObject choice(std::initializer_list<QString> values) {
  QJsonArray a; for (auto const& v : values) a.append(v);
  return {{"type", "string"}, {"enum", a}};
}
QJsonObject tool(QString name, QString description, QJsonObject props = {}, QJsonArray required = {}, bool read = true) {
  return {{"name", name}, {"description", description},
    {"inputSchema", QJsonObject{{"type", "object"}, {"properties", props}, {"required", required}, {"additionalProperties", false}}},
    {"annotations", QJsonObject{{"readOnlyHint", read}, {"destructiveHint", !read}, {"idempotentHint", read}, {"openWorldHint", !read}}}};
}
QString validate(QJsonObject const& args, QJsonObject const& schema) {
  auto props = schema.value("properties").toObject();
  for (auto const& r : schema.value("required").toArray())
    if (!args.contains(r.toString())) return "Missing argument: " + r.toString();
  for (auto it = args.begin(); it != args.end(); ++it) {
    if (!props.contains(it.key())) return "Unknown argument: " + it.key();
    auto spec = props.value(it.key()).toObject(); auto v = it.value(); auto type = spec.value("type").toString();
    if (type == "boolean" && !v.isBool()) return it.key() + " must be boolean";
    if (type == "integer" && (!v.isDouble() || std::floor(v.toDouble()) != v.toDouble()
        || v.toDouble() < spec.value("minimum").toDouble() || v.toDouble() > spec.value("maximum").toDouble()))
      return it.key() + " is outside the integer range";
    if (type == "string" && (!v.isString() || (spec.contains("minLength") && v.toString().size() < spec.value("minLength").toInt())
        || (spec.contains("maxLength") && v.toString().size() > spec.value("maxLength").toInt()))) return it.key() + " must be a bounded string";
    if (spec.contains("enum") && !spec.value("enum").toArray().contains(v)) return "Invalid choice: " + it.key();
  }
  return {};
}
QJsonObject rpcError(QJsonValue id, int code, QString message) {
  return {{"jsonrpc", "2.0"}, {"id", id}, {"error", QJsonObject{{"code", code}, {"message", message}}}};
}
void respond(QTcpSocket *s, int status, QByteArray const& body = {}) {
  if (s->property("replied").toBool()) return;
  s->setProperty("replied", true);
  QByteArray reason = status == 200 ? "OK" : status == 202 ? "Accepted" : status == 401 ? "Unauthorized" : status == 403 ? "Forbidden" : status == 404 ? "Not Found" : status == 405 ? "Method Not Allowed" : status == 413 ? "Payload Too Large" : status == 415 ? "Unsupported Media Type" : status == 503 ? "Service Unavailable" : "Bad Request";
  QByteArray h = "HTTP/1.1 " + QByteArray::number(status) + " " + reason + "\r\nContent-Type: application/json\r\nConnection: close\r\nCache-Control: no-store\r\n";
  if (status == 405) h += "Allow: POST\r\n";
  s->write(h + "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
  s->disconnectFromHost();
}
}

QJsonArray McpServer::tools() {
  return {
    tool("get_status", "Live radio, TX, QSO, watchdog and MCP permission state."),
    tool("get_decodes", "Recent received decodes with stable IDs. Poll after_id; retains the latest 500.", {{"after_id", integer(0, 2147483647)}, {"limit", integer(1, 500)}}),
    tool("get_events", "Recent QSO logging and MCP control events. Poll after_id; retains 500.", {{"after_id", integer(0, 2147483647)}, {"limit", integer(1, 500)}}),
    tool("get_config", "Station identity and operating options; credentials are never returned."),
    tool("get_frequencies", "Configured working frequencies in Hz for the current mode and region."),
    tool("get_messages", "The six generated standard transmit messages and current selection."),
    tool("get_log", "Tail of an active operator ADIF log; file is an optional basename from get_config.log_files, latest file by default.", {{"max_bytes", integer(1, 262144)}, {"file",string(128)}}),
    tool("set_jtty_text", "Prepare JTTY keyboard text without transmitting; start_tx requires prior manual authorization.", {{"text",string(80)}}, {"text"}, false),
    tool("set_monitor", "Enable or disable audio reception.", {{"enabled", boolean()}}, {"enabled"}, false),
    tool("set_frequency", "Set dial frequency in Hz while TX is disabled; requires a configured amateur band.", {{"hz", integer(100000, 2147483647)}}, {"hz"}, false),
    tool("set_mode", "Switch mode while TX is disabled.", {{"mode", choice({"FT8", "FT4", "FT2", "JTTY", "JT65", "JT9", "T10", "WSPR-2", "Q65", "Echo", "MSK144"})}}, {"mode"}, false),
    tool("set_audio_frequency", "Set RX/TX audio frequency in Hz while TX is disabled.", {{"rx_hz", integer(200, 4000)}, {"tx_hz", integer(200, 4000)}}, {}, false),
    tool("set_tx_period", "Select even/first or odd/second TX period while TX is disabled.", {{"first", boolean()}}, {"first"}, false),
    tool("set_target", "Prepare a callsign, optional grid and signal report without enabling TX.", {{"callsign", string(12)}, {"grid", string(6)}, {"report", integer(-50, 49)}}, {"callsign"}, false),
    tool("reply_to_decode", "Select a recent live decoded message by ID, using the normal QSO sequence; does not arm TX.", {{"id", integer(1, 2147483647)}}, {"id"}, false),
    tool("select_message", "Select standard message 1..6 while TX is disabled; does not arm TX.", {{"index", integer(1, 6)}}, {"index"}, false),
    tool("set_options", "Set managed mode, AutoSeq, AutoTX, single QSO, worked-before options or 0..999 minute watchdog. Changes require TX disabled; managed mode never arms TX.",
      {{"managed_mode", choice({"off", "dxcc_hunter", "qso_enthusiast"})}, {"auto_sequence", boolean()}, {"auto_tx", boolean()}, {"single_qso", boolean()}, {"answer_worked", boolean()}, {"call_worked", boolean()}, {"search_cq", boolean()}, {"selection", choice({"manual", "first", "middle", "end"})}, {"watchdog_minutes", integer(0, 999)}}, {}, false),
    tool("clear_target", "Clear current QSO target while TX is disabled.", {}, {}, false),
    tool("start_tx", "Continue an already manually authorized TX session. Cannot arm a stopped station. Requires transmit permission.", {}, {}, false),
    tool("stop_tx", "Immediately halt TX and tune; cancel pending automatic restart. Available even with read-only permissions.", {}, {}, false),
    tool("tune", "Start or stop tuning. Starting requires an active manual TX authorization, transmit permission and configured station.", {{"enabled", boolean()}}, {"enabled"}, false),
    tool("log_qso", "Open the normal log dialog for the current QSO; uses configured auto-log behavior.", {}, {}, false)
  };
}

QJsonObject McpServer::success(QJsonObject const& data) {
  return {{"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact))}}}}, {"structuredContent", data}, {"isError", false}};
}
QJsonObject McpServer::failure(QString const& message) {
  return {{"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", message}}}}, {"isError", true}};
}
McpServer::McpServer(Handler handler, QObject *parent) : QObject(parent), server_(this), handler_(std::move(handler)) {
  connect(&server_, &QTcpServer::newConnection, this, [this] { acceptConnections(); });
}
QString McpServer::configure(McpSettings const& settings) {
  server_.close();
  for (auto socket : server_.findChildren<QTcpSocket*>()) socket->abort();
  settings_ = settings;
  if (!settings.enabled) return {};
  if (settings.port < 1024 || settings.port > 65535 || settings.token.size() < 32)
    return "MCP requires port 1024..65535 and a token of at least 32 characters";
  if (!server_.listen(QHostAddress::LocalHost, static_cast<quint16>(settings.port))) return server_.errorString();
  return {};
}
QJsonObject McpServer::dispatch(QJsonObject const& request) {
  auto id = request.value("id");
  if (request.value("jsonrpc") != "2.0" || !request.value("method").isString()
      || (request.contains("params") && !request.value("params").isObject())
      || (request.contains("id") && !id.isString() && !id.isDouble())) return rpcError(QJsonValue::Null, -32600, "Invalid Request");
  QString method = request.value("method").toString();
  // Notifications must never execute controls, even if they contain tools/call.
  if (!request.contains("id")) return {};
  QJsonObject p = request.value("params").toObject(), result;
  if (method == "initialize") {
    if (!p.value("protocolVersion").isString() || !p.value("capabilities").isObject() || !p.value("clientInfo").isObject())
      return rpcError(id, -32602, "Invalid initialize parameters");
    QString v = p.value("protocolVersion").toString();
    if (v != "2025-03-26" && v != "2025-06-18") v = "2025-11-25";
    result = {{"protocolVersion", v}, {"capabilities", QJsonObject{{"tools", QJsonObject{}}, {"resources", QJsonObject{}}, {"prompts", QJsonObject{}}}},
      {"serverInfo", QJsonObject{{"name", "JTDX-F"}, {"version", QCoreApplication::applicationVersion()}}},
      {"instructions", "Fluxmancer JTDX-F. Read status, prepare station/mode/target, then start_tx only when requested. stop_tx always halts. Poll get_decodes/get_events. RF scheduling and watchdog remain controlled by JTDX-F."}};
  } else if (method == "ping") result = {};
  else if (method == "tools/list") {
    if (p.contains("cursor")) return rpcError(id, -32602, "No pagination cursor supported");
    result = {{"tools", tools()}};
  } else if (method == "tools/call") {
    QJsonObject definition;
    for (auto const& t : tools()) if (t.toObject().value("name") == p.value("name")) definition = t.toObject();
    if (definition.isEmpty()) return rpcError(id, -32602, "Unknown tool");
    if (p.contains("arguments") && !p.value("arguments").isObject()) return rpcError(id, -32602, "arguments must be an object");
    auto args = p.value("arguments").toObject();
    QString error = validate(args, definition.value("inputSchema").toObject());
    if (!error.isEmpty()) return rpcError(id, -32602, error);
    QString name = p.value("name").toString();
    bool stop = name == "stop_tx" || (name == "tune" && !args.value("enabled").toBool());
    if (!settings_.allowControl && !definition.value("annotations").toObject().value("readOnlyHint").toBool() && !stop)
      result = failure("Control is disabled in Settings > MCP");
    else if (!settings_.allowTransmit && (name == "start_tx" || (name == "tune" && !stop)))
      result = failure("Transmit is disabled in Settings > MCP");
    else {
      try { result = handler_(name, args); }
      catch (std::exception const& e) { result = failure(QString::fromUtf8(e.what())); }
    }
  } else if (method == "resources/list") {
    QJsonArray resources;
    for (auto name : {"status", "decodes", "events", "config", "log"}) resources.append(QJsonObject{{"uri", "jtdx://" + QString(name)}, {"name", name}, {"mimeType", "application/json"}});
    result = {{"resources", resources}};
  } else if (method == "resources/templates/list") result = {{"resourceTemplates", QJsonArray{}}};
  else if (method == "resources/read") {
    QString uri = p.value("uri").toString(), name = uri.mid(7);
    if (!uri.startsWith("jtdx://") || !QStringList{"status", "decodes", "events", "config", "log"}.contains(name)) return rpcError(id, -32002, "Unknown resource");
    auto r = handler_("get_" + name, {});
    if (r.value("isError").toBool()) return rpcError(id, -32603, "Resource read failed");
    result = {{"contents", QJsonArray{QJsonObject{{"uri", uri}, {"mimeType", "application/json"}, {"text", QString::fromUtf8(QJsonDocument(r.value("structuredContent").toObject()).toJson(QJsonDocument::Compact))}}}}};
  } else if (method == "prompts/list") result = {{"prompts", QJsonArray{QJsonObject{{"name", "operate_ft8"}, {"description", "FT8 session workflow"}}}}};
  else if (method == "prompts/get" && p.value("name") == "operate_ft8")
    result = {{"messages", QJsonArray{QJsonObject{{"role", "user"}, {"content", QJsonObject{{"type", "text"}, {"text", "Read get_status and get_config. Check station, frequency, mode and watchdog. Prepare target or CQ with TX disabled. Only the operator pressing Enable Tx in the application may authorize a new TX session. start_tx cannot arm a stopped station. Poll decodes/events; let AutoSeq complete QSOs. stop_tx ends the session."}}}}}}};
  else return rpcError(id, -32601, "Method not found");
  return {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}

void McpServer::acceptConnections() {
  while (auto s = server_.nextPendingConnection()) {
    connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
    if (server_.findChildren<QTcpSocket*>().size() > 16) { respond(s, 503); continue; }
    s->setReadBufferSize(73728);
    QTimer::singleShot(10000, s, [s] { s->abort(); });
    auto read = [this, s] {
      if (s->property("replied").toBool()) return;
      QByteArray data = s->property("request").toByteArray() + s->readAll();
      if (data.size() > 73728) { respond(s, 413); return; }
      int split = data.indexOf("\r\n\r\n");
      if (split < 0) { if (data.size() > 8192) respond(s, 413); else s->setProperty("request", data); return; }
      if (split > 8192) { respond(s, 413); return; }
      auto lines = data.left(split).split('\n'); auto start = lines.takeFirst().trimmed().split(' ');
      if (start.size() != 3 || start[2] != "HTTP/1.1") { respond(s, 400); return; }
      QHash<QByteArray, QByteArray> h;
      for (auto line : lines) {
        int colon = line.indexOf(':');
        if (colon < 1) { respond(s, 400); return; }
        auto key = line.left(colon).trimmed().toLower();
        if (h.contains(key)) { respond(s, 400); return; }
        h.insert(key, line.mid(colon + 1).trimmed());
      }
      QByteArray port = QByteArray::number(settings_.port);
      if (h.value("host") != "127.0.0.1:" + port && h.value("host") != "localhost:" + port) { respond(s, 403); return; }
      if (h.contains("origin") && h.value("origin") != "http://127.0.0.1:" + port && h.value("origin") != "http://localhost:" + port) { respond(s, 403); return; }
      if (h.value("authorization") != "Bearer " + settings_.token.toUtf8()) { respond(s, 401); return; }
      if (start[1] != "/mcp") { respond(s, 404); return; }
      if (start[0] != "POST") { respond(s, 405); return; }
      if (h.value("content-type").split(';').first().trimmed().toLower() != "application/json") { respond(s, 415); return; }
      if (h.contains("transfer-encoding")) { respond(s, 400); return; }
      auto version = h.value("mcp-protocol-version");
      if (!version.isEmpty() && version != "2025-03-26" && version != "2025-06-18" && version != "2025-11-25") { respond(s, 400); return; }
      bool ok = false; qint64 size = h.value("content-length").toLongLong(&ok);
      if (!ok || size < 0) { respond(s, 400); return; }
      if (size > 65536) { respond(s, 413); return; }
      if (data.size() - split - 4 < size) { s->setProperty("request", data); return; }
      if (data.size() - split - 4 != size) { respond(s, 400); return; }
      QJsonParseError error; auto doc = QJsonDocument::fromJson(data.mid(split + 4), &error);
      QJsonObject reply;
      if (error.error != QJsonParseError::NoError) reply = rpcError(QJsonValue::Null, -32700, "Parse error");
      else if (!doc.isObject()) reply = rpcError(QJsonValue::Null, -32600, "Single request object required");
      else reply = dispatch(doc.object());
      respond(s, reply.isEmpty() ? 202 : 200, reply.isEmpty() ? QByteArray{} : QJsonDocument(reply).toJson(QJsonDocument::Compact));
    };
    connect(s, &QTcpSocket::readyRead, this, read);
    if (s->bytesAvailable()) read();
  }
}
