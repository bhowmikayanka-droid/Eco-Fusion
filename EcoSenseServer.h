/*
  EcoSenseServer.h — tiny HTTP server for ESP32 built only on WiFi.h
  (WiFiServer / WiFiClient). Replaces <WebServer.h>.

  Supports what the EcoSense dashboard needs:
    on(path, method, handler), onNotFound(handler), begin(), handleClient(),
    uri(), method(), sendHeader(), send(), send_P()

  Place this file in the same folder as EcoSenseDashboard.ino.
*/
#ifndef ECOSENSE_SERVER_H
#define ECOSENSE_SERVER_H

#include <Arduino.h>
#include <WiFi.h>
#include <functional>

// Own method names so nothing clashes with other libraries' HTTP_GET macros.
enum EcoMethod { ECO_GET, ECO_POST, ECO_OTHER };

class EcoSenseServer {
 public:
  typedef std::function<void()> Handler;

  explicit EcoSenseServer(uint16_t port = 80) : _server(port) {}

  void begin() { _server.begin(); }

  void on(const char* path, EcoMethod method, Handler handler) {
    if (_routeCount < MAX_ROUTES) {
      _routes[_routeCount].path = path;
      _routes[_routeCount].method = method;
      _routes[_routeCount].handler = handler;
      _routeCount++;
    }
  }

  void onNotFound(Handler handler) { _notFound = handler; }

  String uri() const { return _uri; }
  EcoMethod method() const { return _method; }

  void sendHeader(const char* name, const char* value) {
    _extraHeaders += name;
    _extraHeaders += ": ";
    _extraHeaders += value;
    _extraHeaders += "\r\n";
  }

  void send(int code, const char* contentType, const String& body) {
    writeHead(code, contentType, body.length());
    if (_method != ECO_OTHER) _client.write((const uint8_t*)body.c_str(), body.length());
  }

  void send(int code, const char* contentType, const char* body) {
    send(code, contentType, String(body));
  }

  // Send a (possibly large) constant string; written in chunks to save RAM.
  void send_P(int code, const char* contentType, const char* body) {
    size_t len = strlen(body);
    writeHead(code, contentType, len);
    size_t sent = 0;
    while (sent < len && _client.connected()) {
      size_t n = len - sent;
      if (n > 1024) n = 1024;
      size_t w = _client.write((const uint8_t*)(body + sent), n);
      if (w == 0) { delay(1); if (++_stall > 2000) break; continue; }
      _stall = 0;
      sent += w;
    }
  }

  void handleClient() {
    _client = _server.available();
    if (!_client) return;

    // Wait (briefly) for the request line.
    unsigned long start = millis();
    while (_client.connected() && !_client.available()) {
      if (millis() - start > 1500) { _client.stop(); return; }
      delay(1);
    }

    String requestLine = _client.readStringUntil('\n');
    requestLine.trim();

    // Skip remaining request headers.
    start = millis();
    while (_client.connected() && millis() - start < 1500) {
      if (!_client.available()) { delay(1); continue; }
      String line = _client.readStringUntil('\n');
      if (line == "\r" || line.length() == 0) break;
    }

    // Parse "GET /path?query HTTP/1.1"
    int sp1 = requestLine.indexOf(' ');
    int sp2 = requestLine.indexOf(' ', sp1 + 1);
    if (sp1 < 0 || sp2 < 0) { _client.stop(); return; }

    String m = requestLine.substring(0, sp1);
    _uri = requestLine.substring(sp1 + 1, sp2);
    int q = _uri.indexOf('?');
    if (q >= 0) _uri = _uri.substring(0, q);

    if (m == "GET") _method = ECO_GET;
    else if (m == "POST") _method = ECO_POST;
    else if (m == "HEAD") _method = ECO_OTHER; // headers only
    else _method = ECO_OTHER;

    _extraHeaders = "";
    _stall = 0;

    bool handled = false;
    for (int i = 0; i < _routeCount; i++) {
      if (_routes[i].method == _method && _uri == _routes[i].path) {
        _routes[i].handler();
        handled = true;
        break;
      }
    }
    if (!handled) {
      if (_notFound) _notFound();
      else send(404, "text/plain", "Not found");
    }

    _client.flush();
    _client.stop();
  }

 private:
  static const int MAX_ROUTES = 8;
  struct Route {
    const char* path;
    EcoMethod method;
    Handler handler;
  };

  void writeHead(int code, const char* contentType, size_t length) {
    const char* text = "OK";
    if (code == 404) text = "Not Found";
    else if (code == 400) text = "Bad Request";
    else if (code == 500) text = "Internal Server Error";

    _client.print("HTTP/1.1 ");
    _client.print(code);
    _client.print(' ');
    _client.print(text);
    _client.print("\r\nContent-Type: ");
    _client.print(contentType);
    _client.print("\r\nContent-Length: ");
    _client.print((unsigned long)length);
    _client.print("\r\nConnection: close\r\n");
    _client.print(_extraHeaders);
    _client.print("\r\n");
  }

  WiFiServer _server;
  WiFiClient _client;
  Route _routes[MAX_ROUTES];
  int _routeCount = 0;
  Handler _notFound;
  String _uri;
  EcoMethod _method = ECO_GET;
  String _extraHeaders;
  int _stall = 0;
};

#endif
