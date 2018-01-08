#pragma once

#include <ChakraCore.h>

class IJsrtInspectorCallback {
public:
  virtual void SendResponse(int callId, const char* response) = 0;
  virtual void SendNotification(const char* notification) = 0;

  virtual void RunMessageLoopOnPause() = 0;
  virtual void QuitMessageLoopOnPause() = 0;
};

class JsrtInspector {
public:
  static JsrtInspector Enable(JsRuntimeHandle* runtime);
  void Dispose();

  void Connect(IJsrtInspectorCallback* callback);
  void SendCommand(const char* command);
  void Disconnect();

private:
  JsrtInspector(JsRuntimeHandle* runtime);

  bool connected;
  JsRuntimeHandle* runtime;
};