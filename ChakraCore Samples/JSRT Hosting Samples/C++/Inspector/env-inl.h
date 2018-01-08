// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

#ifndef SRC_ENV_INL_H_
#define SRC_ENV_INL_H_

#if defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#include "env.h"
#include "node.h"
#include "util.h"
#include "util-inl.h"
#include "uv.h"
#include "v8.h"

#include <stddef.h>
#include <stdint.h>

namespace node {

inline IsolateData::IsolateData(v8::Isolate* isolate, uv_loop_t* event_loop,
                                uint32_t* zero_fill_field) :

// Create string and private symbol properties as internalized one byte strings.
//
// Internalized because it makes property lookups a little faster and because
// the string is created in the old space straight away.  It's going to end up
// in the old space sooner or later anyway but now it doesn't go through
// v8::Eternal's new space handling first.
//
// One byte because our strings are ASCII and we can safely skip V8's UTF-8
// decoding step.  It's a one-time cost, but why pay it when you don't have to?
#define V(PropertyName, StringValue)                                          \
    PropertyName ## _(                                                        \
        isolate,                                                              \
        v8::Private::New(                                                     \
            isolate,                                                          \
            v8::String::NewFromOneByte(                                       \
                isolate,                                                      \
                reinterpret_cast<const uint8_t*>(StringValue),                \
                v8::NewStringType::kInternalized,                             \
                sizeof(StringValue) - 1).ToLocalChecked())),
  PER_ISOLATE_PRIVATE_SYMBOL_PROPERTIES(V)
#undef V
#define V(PropertyName, StringValue)                                          \
    PropertyName ## _(                                                        \
        isolate,                                                              \
        v8::String::NewFromOneByte(                                           \
            isolate,                                                          \
            reinterpret_cast<const uint8_t*>(StringValue),                    \
            v8::NewStringType::kInternalized,                                 \
            sizeof(StringValue) - 1).ToLocalChecked()),
    PER_ISOLATE_STRING_PROPERTIES(V)
#undef V
    event_loop_(event_loop), zero_fill_field_(zero_fill_field) {}

inline uv_loop_t* IsolateData::event_loop() const {
  return event_loop_;
}

inline uint32_t* IsolateData::zero_fill_field() const {
  return zero_fill_field_;
}

inline Environment::AsyncCallbackScope::AsyncCallbackScope(Environment* env)
    : env_(env) {
  env_->makecallback_cntr_++;
}

inline Environment::AsyncCallbackScope::~AsyncCallbackScope() {
  env_->makecallback_cntr_--;
}

inline bool Environment::AsyncCallbackScope::in_makecallback() {
  return env_->makecallback_cntr_ > 1;
}

inline Environment::DomainFlag::DomainFlag() {
  for (int i = 0; i < kFieldsCount; ++i) fields_[i] = 0;
}

inline uint32_t* Environment::DomainFlag::fields() {
  return fields_;
}

inline int Environment::DomainFlag::fields_count() const {
  return kFieldsCount;
}

inline uint32_t Environment::DomainFlag::count() const {
  return fields_[kCount];
}

inline Environment::TickInfo::TickInfo() {
  for (int i = 0; i < kFieldsCount; ++i)
    fields_[i] = 0;
}

inline uint32_t* Environment::TickInfo::fields() {
  return fields_;
}

inline int Environment::TickInfo::fields_count() const {
  return kFieldsCount;
}

inline uint32_t Environment::TickInfo::index() const {
  return fields_[kIndex];
}

inline uint32_t Environment::TickInfo::length() const {
  return fields_[kLength];
}

inline void Environment::TickInfo::set_index(uint32_t value) {
  fields_[kIndex] = value;
}

inline void Environment::AssignToContext(v8::Local<v8::Context> context) {
  context->SetAlignedPointerInEmbedderData(kContextEmbedderDataIndex, this);
}

inline Environment* Environment::GetCurrent(v8::Isolate* isolate) {
  return GetCurrent(isolate->GetCurrentContext());
}

inline Environment* Environment::GetCurrent(v8::Local<v8::Context> context) {
  return static_cast<Environment*>(
      context->GetAlignedPointerFromEmbedderData(kContextEmbedderDataIndex));
}

inline Environment* Environment::GetCurrent(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
  CHECK(info.Data()->IsExternal());
  return static_cast<Environment*>(info.Data().As<v8::External>()->Value());
}

template <typename T>
inline Environment* Environment::GetCurrent(
    const v8::PropertyCallbackInfo<T>& info) {
  CHECK(info.Data()->IsExternal());
  // XXX(bnoordhuis) Work around a g++ 4.9.2 template type inferrer bug
  // when the expression is written as info.Data().As<v8::External>().
  v8::Local<v8::Value> data = info.Data();
  return static_cast<Environment*>(data.As<v8::External>()->Value());
}

inline Environment::Environment(IsolateData* isolate_data,
                                v8::Local<v8::Context> context)
    : isolate_(context->GetIsolate()),
      isolate_data_(isolate_data),
      timer_base_(uv_now(isolate_data->event_loop())),
      cares_query_last_ok_(true),
      cares_is_servers_default_(true),
      using_domains_(false),
      printed_error_(false),
      trace_sync_io_(false),
      abort_on_uncaught_exception_(false),
      makecallback_cntr_(0),
#if HAVE_INSPECTOR
      inspector_agent_(this),
#endif
      handle_cleanup_waiting_(0),
      http_parser_buffer_(nullptr),
      fs_stats_field_array_(),
      context_(context->GetIsolate(), context) {
  // We'll be creating new objects so make sure we've entered the context.
  v8::HandleScope handle_scope(isolate());
  v8::Context::Scope context_scope(context);
  set_as_external(v8::External::New(isolate(), this));
  set_binding_cache_object(v8::Object::New(isolate()));
  set_module_load_list_array(v8::Array::New(isolate()));

  AssignToContext(context);

  destroy_ids_list_.reserve(512);
}

inline Environment::~Environment() {
  v8::HandleScope handle_scope(isolate());

  fs_stats_field_array_.Empty();

  context()->SetAlignedPointerInEmbedderData(kContextEmbedderDataIndex,
                                             nullptr);
#define V(PropertyName, TypeName) PropertyName ## _.Reset();
  ENVIRONMENT_STRONG_PERSISTENT_PROPERTIES(V)
#undef V

  delete[] heap_statistics_buffer_;
  delete[] heap_space_statistics_buffer_;
  delete[] http_parser_buffer_;
}

inline v8::Isolate* Environment::isolate() const {
  return isolate_;
}

inline bool Environment::in_domain() const {
  // The const_cast is okay, it doesn't violate conceptual const-ness.
  return using_domains() &&
         const_cast<Environment*>(this)->domain_flag()->count() > 0;
}

inline Environment* Environment::from_immediate_check_handle(
    uv_check_t* handle) {
  return ContainerOf(&Environment::immediate_check_handle_, handle);
}

inline uv_check_t* Environment::immediate_check_handle() {
  return &immediate_check_handle_;
}

inline uv_idle_t* Environment::immediate_idle_handle() {
  return &immediate_idle_handle_;
}

inline Environment* Environment::from_destroy_ids_timer_handle(
    uv_timer_t* handle) {
  return ContainerOf(&Environment::destroy_ids_timer_handle_, handle);
}

inline uv_timer_t* Environment::destroy_ids_timer_handle() {
  return &destroy_ids_timer_handle_;
}

inline void Environment::RegisterHandleCleanup(uv_handle_t* handle,
                                               HandleCleanupCb cb,
                                               void *arg) {
  handle_cleanup_queue_.PushBack(new HandleCleanup(handle, cb, arg));
}

inline void Environment::FinishHandleCleanup(uv_handle_t* handle) {
  handle_cleanup_waiting_--;
}

inline uv_loop_t* Environment::event_loop() const {
  return isolate_data()->event_loop();
}

inline Environment::DomainFlag* Environment::domain_flag() {
  return &domain_flag_;
}

inline Environment::TickInfo* Environment::tick_info() {
  return &tick_info_;
}

inline uint64_t Environment::timer_base() const {
  return timer_base_;
}

inline bool Environment::using_domains() const {
  return using_domains_;
}

inline void Environment::set_using_domains(bool value) {
  using_domains_ = value;
}

inline bool Environment::printed_error() const {
  return printed_error_;
}

inline void Environment::set_printed_error(bool value) {
  printed_error_ = value;
}

inline void Environment::set_trace_sync_io(bool value) {
  trace_sync_io_ = value;
}

inline bool Environment::abort_on_uncaught_exception() const {
  return abort_on_uncaught_exception_;
}

inline void Environment::set_abort_on_uncaught_exception(bool value) {
  abort_on_uncaught_exception_ = value;
}

inline std::vector<double>* Environment::destroy_ids_list() {
  return &destroy_ids_list_;
}

inline double* Environment::heap_statistics_buffer() const {
  CHECK_NE(heap_statistics_buffer_, nullptr);
  return heap_statistics_buffer_;
}

inline void Environment::set_heap_statistics_buffer(double* pointer) {
  CHECK_EQ(heap_statistics_buffer_, nullptr);  // Should be set only once.
  heap_statistics_buffer_ = pointer;
}

inline double* Environment::heap_space_statistics_buffer() const {
  CHECK_NE(heap_space_statistics_buffer_, nullptr);
  return heap_space_statistics_buffer_;
}

inline void Environment::set_heap_space_statistics_buffer(double* pointer) {
  CHECK_EQ(heap_space_statistics_buffer_, nullptr);  // Should be set only once.
  heap_space_statistics_buffer_ = pointer;
}


inline char* Environment::http_parser_buffer() const {
  return http_parser_buffer_;
}

inline void Environment::set_http_parser_buffer(char* buffer) {
  CHECK_EQ(http_parser_buffer_, nullptr);  // Should be set only once.
  http_parser_buffer_ = buffer;
}

inline v8::Local<v8::Float64Array> Environment::fs_stats_field_array() const {
  return v8::Local<v8::Float64Array>::New(isolate_, fs_stats_field_array_);
}

inline void Environment::set_fs_stats_field_array(
    v8::Local<v8::Float64Array> fields) {
  CHECK_EQ(fs_stats_field_array_.IsEmpty(), true);  // Should be set only once.
  fs_stats_field_array_ = v8::Global<v8::Float64Array>(isolate_, fields);
}

inline Environment* Environment::from_cares_timer_handle(uv_timer_t* handle) {
  return ContainerOf(&Environment::cares_timer_handle_, handle);
}

inline uv_timer_t* Environment::cares_timer_handle() {
  return &cares_timer_handle_;
}

inline bool Environment::cares_query_last_ok() {
  return cares_query_last_ok_;
}

inline void Environment::set_cares_query_last_ok(bool ok) {
  cares_query_last_ok_ = ok;
}

inline bool Environment::cares_is_servers_default() {
  return cares_is_servers_default_;
}

inline void Environment::set_cares_is_servers_default(bool is_default) {
  cares_is_servers_default_ = is_default;
}

inline IsolateData* Environment::isolate_data() const {
  return isolate_data_;
}

inline void Environment::ThrowError(const char* errmsg) {
  ThrowError(v8::Exception::Error, errmsg);
}

inline void Environment::ThrowTypeError(const char* errmsg) {
  ThrowError(v8::Exception::TypeError, errmsg);
}

inline void Environment::ThrowRangeError(const char* errmsg) {
  ThrowError(v8::Exception::RangeError, errmsg);
}

inline void Environment::ThrowError(
    v8::Local<v8::Value> (*fun)(v8::Local<v8::String>),
    const char* errmsg) {
  v8::HandleScope handle_scope(isolate());
  isolate()->ThrowException(fun(OneByteString(isolate(), errmsg)));
}

inline void Environment::ThrowErrnoException(int errorno,
                                             const char* syscall,
                                             const char* message,
                                             const char* path) {
  isolate()->ThrowException(
      ErrnoException(isolate(), errorno, syscall, message, path));
}

inline void Environment::ThrowUVException(int errorno,
                                          const char* syscall,
                                          const char* message,
                                          const char* path,
                                          const char* dest) {
  isolate()->ThrowException(
      UVException(isolate(), errorno, syscall, message, path, dest));
}

inline v8::Local<v8::FunctionTemplate>
    Environment::NewFunctionTemplate(v8::FunctionCallback callback,
                                     v8::Local<v8::Signature> signature) {
  v8::Local<v8::External> external = as_external();
  return v8::FunctionTemplate::New(isolate(), callback, external, signature);
}

inline void Environment::SetMethod(v8::Local<v8::Object> that,
                                   const char* name,
                                   v8::FunctionCallback callback) {
  v8::Local<v8::Function> function =
      NewFunctionTemplate(callback)->GetFunction();
  // kInternalized strings are created in the old space.
  const v8::NewStringType type = v8::NewStringType::kInternalized;
  v8::Local<v8::String> name_string =
      v8::String::NewFromUtf8(isolate(), name, type).ToLocalChecked();
  that->Set(name_string, function);
  function->SetName(name_string);  // NODE_SET_METHOD() compatibility.
}

inline void Environment::SetProtoMethod(v8::Local<v8::FunctionTemplate> that,
                                        const char* name,
                                        v8::FunctionCallback callback) {
  v8::Local<v8::Signature> signature = v8::Signature::New(isolate(), that);
  v8::Local<v8::FunctionTemplate> t = NewFunctionTemplate(callback, signature);
  // kInternalized strings are created in the old space.
  const v8::NewStringType type = v8::NewStringType::kInternalized;
  v8::Local<v8::String> name_string =
      v8::String::NewFromUtf8(isolate(), name, type).ToLocalChecked();
  that->PrototypeTemplate()->Set(name_string, t);
  t->SetClassName(name_string);  // NODE_SET_PROTOTYPE_METHOD() compatibility.
}

inline void Environment::SetTemplateMethod(v8::Local<v8::FunctionTemplate> that,
                                           const char* name,
                                           v8::FunctionCallback callback) {
  v8::Local<v8::FunctionTemplate> t = NewFunctionTemplate(callback);
  // kInternalized strings are created in the old space.
  const v8::NewStringType type = v8::NewStringType::kInternalized;
  v8::Local<v8::String> name_string =
      v8::String::NewFromUtf8(isolate(), name, type).ToLocalChecked();
  that->Set(name_string, t);
  t->SetClassName(name_string);  // NODE_SET_METHOD() compatibility.
}

#define VP(PropertyName, StringValue) V(v8::Private, PropertyName)
#define VS(PropertyName, StringValue) V(v8::String, PropertyName)
#define V(TypeName, PropertyName)                                             \
  inline                                                                      \
  v8::Local<TypeName> IsolateData::PropertyName(v8::Isolate* isolate) const { \
    /* Strings are immutable so casting away const-ness here is okay. */      \
    return const_cast<IsolateData*>(this)->PropertyName ## _.Get(isolate);    \
  }
  PER_ISOLATE_PRIVATE_SYMBOL_PROPERTIES(VP)
  PER_ISOLATE_STRING_PROPERTIES(VS)
#undef V
#undef VS
#undef VP

#define VP(PropertyName, StringValue) V(v8::Private, PropertyName)
#define VS(PropertyName, StringValue) V(v8::String, PropertyName)
#define V(TypeName, PropertyName)                                             \
  inline v8::Local<TypeName> Environment::PropertyName() const {              \
    return isolate_data()->PropertyName(isolate());                           \
  }
  PER_ISOLATE_PRIVATE_SYMBOL_PROPERTIES(VP)
  PER_ISOLATE_STRING_PROPERTIES(VS)
#undef V
#undef VS
#undef VP

#define V(PropertyName, TypeName)                                             \
  inline v8::Local<TypeName> Environment::PropertyName() const {              \
    return StrongPersistentToLocal(PropertyName ## _);                        \
  }                                                                           \
  inline void Environment::set_ ## PropertyName(v8::Local<TypeName> value) {  \
    PropertyName ## _.Reset(isolate(), value);                                \
  }
  ENVIRONMENT_STRONG_PERSISTENT_PROPERTIES(V)
#undef V

}  // namespace node

#endif  // defined(NODE_WANT_INTERNALS) && NODE_WANT_INTERNALS

#endif  // SRC_ENV_INL_H_
