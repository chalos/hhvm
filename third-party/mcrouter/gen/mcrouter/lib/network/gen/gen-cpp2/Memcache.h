/**
 * Autogenerated by Thrift for MemcacheService.thrift
 *
 * DO NOT EDIT UNLESS YOU ARE SURE THAT YOU KNOW WHAT YOU ARE DOING
 *  @generated
 */
#pragma once

#include <thrift/lib/cpp2/gen/service_h.h>

#include "mcrouter/lib/network/gen/gen-cpp2/MemcacheAsyncClient.h"
#include "mcrouter/lib/network/gen/gen-cpp2/MemcacheService_types.h"
#include "mcrouter/lib/network/gen/gen-cpp2/Common_types.h"
#include "mcrouter/lib/network/gen/gen-cpp2/Memcache_types.h"
#include "mcrouter/lib/network/gen/MemcacheMessages.h"

namespace folly {
  class IOBuf;
  class IOBufQueue;
}
namespace apache { namespace thrift {
  class Cpp2RequestContext;
  class BinaryProtocolReader;
  class CompactProtocolReader;
  namespace transport { class THeader; }
}}

namespace facebook { namespace memcache { namespace thrift {

class MemcacheSvAsyncIf {
 public:
  virtual ~MemcacheSvAsyncIf() {}
  virtual void async_eb_mcAdd(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McAddReply>> callback, const facebook::memcache::McAddRequest& request) = 0;
  virtual void async_eb_mcAppend(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McAppendReply>> callback, const facebook::memcache::McAppendRequest& request) = 0;
  virtual void async_eb_mcCas(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McCasReply>> callback, const facebook::memcache::McCasRequest& request) = 0;
  virtual void async_eb_mcDecr(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McDecrReply>> callback, const facebook::memcache::McDecrRequest& request) = 0;
  virtual void async_eb_mcDelete(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McDeleteReply>> callback, const facebook::memcache::McDeleteRequest& request) = 0;
  virtual void async_eb_mcFlushAll(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McFlushAllReply>> callback, const facebook::memcache::McFlushAllRequest& request) = 0;
  virtual void async_eb_mcFlushRe(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McFlushReReply>> callback, const facebook::memcache::McFlushReRequest& request) = 0;
  virtual void async_eb_mcGat(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McGatReply>> callback, const facebook::memcache::McGatRequest& request) = 0;
  virtual void async_eb_mcGats(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McGatsReply>> callback, const facebook::memcache::McGatsRequest& request) = 0;
  virtual void async_eb_mcGet(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McGetReply>> callback, const facebook::memcache::McGetRequest& request) = 0;
  virtual void async_eb_mcGets(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McGetsReply>> callback, const facebook::memcache::McGetsRequest& request) = 0;
  virtual void async_eb_mcIncr(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McIncrReply>> callback, const facebook::memcache::McIncrRequest& request) = 0;
  virtual void async_eb_mcLeaseGet(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McLeaseGetReply>> callback, const facebook::memcache::McLeaseGetRequest& request) = 0;
  virtual void async_eb_mcLeaseSet(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McLeaseSetReply>> callback, const facebook::memcache::McLeaseSetRequest& request) = 0;
  virtual void async_eb_mcMetaget(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McMetagetReply>> callback, const facebook::memcache::McMetagetRequest& request) = 0;
  virtual void async_eb_mcPrepend(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McPrependReply>> callback, const facebook::memcache::McPrependRequest& request) = 0;
  virtual void async_eb_mcReplace(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McReplaceReply>> callback, const facebook::memcache::McReplaceRequest& request) = 0;
  virtual void async_eb_mcSet(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McSetReply>> callback, const facebook::memcache::McSetRequest& request) = 0;
  virtual void async_eb_mcTouch(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McTouchReply>> callback, const facebook::memcache::McTouchRequest& request) = 0;
  virtual void async_eb_mcVersion(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McVersionReply>> callback, const facebook::memcache::McVersionRequest& request) = 0;
};

class MemcacheAsyncProcessor;

class MemcacheSvIf : public MemcacheSvAsyncIf, public apache::thrift::ServerInterface {
 public:
  typedef MemcacheAsyncProcessor ProcessorType;
  std::unique_ptr<apache::thrift::AsyncProcessor> getProcessor() override;


  void async_eb_mcAdd(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McAddReply>> callback, const facebook::memcache::McAddRequest& request) override;
  void async_eb_mcAppend(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McAppendReply>> callback, const facebook::memcache::McAppendRequest& request) override;
  void async_eb_mcCas(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McCasReply>> callback, const facebook::memcache::McCasRequest& request) override;
  void async_eb_mcDecr(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McDecrReply>> callback, const facebook::memcache::McDecrRequest& request) override;
  void async_eb_mcDelete(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McDeleteReply>> callback, const facebook::memcache::McDeleteRequest& request) override;
  void async_eb_mcFlushAll(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McFlushAllReply>> callback, const facebook::memcache::McFlushAllRequest& request) override;
  void async_eb_mcFlushRe(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McFlushReReply>> callback, const facebook::memcache::McFlushReRequest& request) override;
  void async_eb_mcGat(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McGatReply>> callback, const facebook::memcache::McGatRequest& request) override;
  void async_eb_mcGats(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McGatsReply>> callback, const facebook::memcache::McGatsRequest& request) override;
  void async_eb_mcGet(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McGetReply>> callback, const facebook::memcache::McGetRequest& request) override;
  void async_eb_mcGets(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McGetsReply>> callback, const facebook::memcache::McGetsRequest& request) override;
  void async_eb_mcIncr(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McIncrReply>> callback, const facebook::memcache::McIncrRequest& request) override;
  void async_eb_mcLeaseGet(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McLeaseGetReply>> callback, const facebook::memcache::McLeaseGetRequest& request) override;
  void async_eb_mcLeaseSet(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McLeaseSetReply>> callback, const facebook::memcache::McLeaseSetRequest& request) override;
  void async_eb_mcMetaget(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McMetagetReply>> callback, const facebook::memcache::McMetagetRequest& request) override;
  void async_eb_mcPrepend(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McPrependReply>> callback, const facebook::memcache::McPrependRequest& request) override;
  void async_eb_mcReplace(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McReplaceReply>> callback, const facebook::memcache::McReplaceRequest& request) override;
  void async_eb_mcSet(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McSetReply>> callback, const facebook::memcache::McSetRequest& request) override;
  void async_eb_mcTouch(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McTouchReply>> callback, const facebook::memcache::McTouchRequest& request) override;
  void async_eb_mcVersion(std::unique_ptr<apache::thrift::HandlerCallback<facebook::memcache::McVersionReply>> callback, const facebook::memcache::McVersionRequest& request) override;
};

class MemcacheSvNull : public MemcacheSvIf {
 public:
};

class MemcacheAsyncProcessor : public ::apache::thrift::GeneratedAsyncProcessor {
 public:
  const char* getServiceName() override;
  void getServiceMetadata(apache::thrift::metadata::ThriftServiceMetadataResponse& response) override;
  using BaseAsyncProcessor = void;
 protected:
  MemcacheSvIf* iface_;
 public:
  void processSerializedRequest(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::protocol::PROTOCOL_TYPES protType, apache::thrift::Cpp2RequestContext* context, folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm) override;
 protected:
  std::shared_ptr<folly::RequestContext> getBaseContextForRequest() override;
 public:
  using ProcessFunc = GeneratedAsyncProcessor::ProcessFunc<MemcacheAsyncProcessor>;
  using ProcessMap = GeneratedAsyncProcessor::ProcessMap<ProcessFunc>;
  static const MemcacheAsyncProcessor::ProcessMap& getBinaryProtocolProcessMap();
  static const MemcacheAsyncProcessor::ProcessMap& getCompactProtocolProcessMap();
 private:
  static const MemcacheAsyncProcessor::ProcessMap binaryProcessMap_;
  static const MemcacheAsyncProcessor::ProcessMap compactProcessMap_;
 private:
  template <typename ProtocolIn_, typename ProtocolOut_>
  void setUpAndProcess_mcAdd(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx, folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <typename ProtocolIn_, typename ProtocolOut_>
  void process_mcAdd(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx,folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <class ProtocolIn_, class ProtocolOut_>
  static folly::IOBufQueue return_mcAdd(int32_t protoSeqId, apache::thrift::ContextStack* ctx, facebook::memcache::McAddReply const& _return);
  template <class ProtocolIn_, class ProtocolOut_>
  static void throw_wrapped_mcAdd(apache::thrift::ResponseChannelRequest::UniquePtr req,int32_t protoSeqId,apache::thrift::ContextStack* ctx,folly::exception_wrapper ew,apache::thrift::Cpp2RequestContext* reqCtx);
  template <typename ProtocolIn_, typename ProtocolOut_>
  void setUpAndProcess_mcAppend(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx, folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <typename ProtocolIn_, typename ProtocolOut_>
  void process_mcAppend(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx,folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <class ProtocolIn_, class ProtocolOut_>
  static folly::IOBufQueue return_mcAppend(int32_t protoSeqId, apache::thrift::ContextStack* ctx, facebook::memcache::McAppendReply const& _return);
  template <class ProtocolIn_, class ProtocolOut_>
  static void throw_wrapped_mcAppend(apache::thrift::ResponseChannelRequest::UniquePtr req,int32_t protoSeqId,apache::thrift::ContextStack* ctx,folly::exception_wrapper ew,apache::thrift::Cpp2RequestContext* reqCtx);
  template <typename ProtocolIn_, typename ProtocolOut_>
  void setUpAndProcess_mcCas(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx, folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <typename ProtocolIn_, typename ProtocolOut_>
  void process_mcCas(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx,folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <class ProtocolIn_, class ProtocolOut_>
  static folly::IOBufQueue return_mcCas(int32_t protoSeqId, apache::thrift::ContextStack* ctx, facebook::memcache::McCasReply const& _return);
  template <class ProtocolIn_, class ProtocolOut_>
  static void throw_wrapped_mcCas(apache::thrift::ResponseChannelRequest::UniquePtr req,int32_t protoSeqId,apache::thrift::ContextStack* ctx,folly::exception_wrapper ew,apache::thrift::Cpp2RequestContext* reqCtx);
  template <typename ProtocolIn_, typename ProtocolOut_>
  void setUpAndProcess_mcDecr(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx, folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <typename ProtocolIn_, typename ProtocolOut_>
  void process_mcDecr(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx,folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <class ProtocolIn_, class ProtocolOut_>
  static folly::IOBufQueue return_mcDecr(int32_t protoSeqId, apache::thrift::ContextStack* ctx, facebook::memcache::McDecrReply const& _return);
  template <class ProtocolIn_, class ProtocolOut_>
  static void throw_wrapped_mcDecr(apache::thrift::ResponseChannelRequest::UniquePtr req,int32_t protoSeqId,apache::thrift::ContextStack* ctx,folly::exception_wrapper ew,apache::thrift::Cpp2RequestContext* reqCtx);
  template <typename ProtocolIn_, typename ProtocolOut_>
  void setUpAndProcess_mcDelete(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx, folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <typename ProtocolIn_, typename ProtocolOut_>
  void process_mcDelete(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx,folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <class ProtocolIn_, class ProtocolOut_>
  static folly::IOBufQueue return_mcDelete(int32_t protoSeqId, apache::thrift::ContextStack* ctx, facebook::memcache::McDeleteReply const& _return);
  template <class ProtocolIn_, class ProtocolOut_>
  static void throw_wrapped_mcDelete(apache::thrift::ResponseChannelRequest::UniquePtr req,int32_t protoSeqId,apache::thrift::ContextStack* ctx,folly::exception_wrapper ew,apache::thrift::Cpp2RequestContext* reqCtx);
  template <typename ProtocolIn_, typename ProtocolOut_>
  void setUpAndProcess_mcFlushAll(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx, folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <typename ProtocolIn_, typename ProtocolOut_>
  void process_mcFlushAll(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx,folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <class ProtocolIn_, class ProtocolOut_>
  static folly::IOBufQueue return_mcFlushAll(int32_t protoSeqId, apache::thrift::ContextStack* ctx, facebook::memcache::McFlushAllReply const& _return);
  template <class ProtocolIn_, class ProtocolOut_>
  static void throw_wrapped_mcFlushAll(apache::thrift::ResponseChannelRequest::UniquePtr req,int32_t protoSeqId,apache::thrift::ContextStack* ctx,folly::exception_wrapper ew,apache::thrift::Cpp2RequestContext* reqCtx);
  template <typename ProtocolIn_, typename ProtocolOut_>
  void setUpAndProcess_mcFlushRe(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx, folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <typename ProtocolIn_, typename ProtocolOut_>
  void process_mcFlushRe(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx,folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <class ProtocolIn_, class ProtocolOut_>
  static folly::IOBufQueue return_mcFlushRe(int32_t protoSeqId, apache::thrift::ContextStack* ctx, facebook::memcache::McFlushReReply const& _return);
  template <class ProtocolIn_, class ProtocolOut_>
  static void throw_wrapped_mcFlushRe(apache::thrift::ResponseChannelRequest::UniquePtr req,int32_t protoSeqId,apache::thrift::ContextStack* ctx,folly::exception_wrapper ew,apache::thrift::Cpp2RequestContext* reqCtx);
  template <typename ProtocolIn_, typename ProtocolOut_>
  void setUpAndProcess_mcGat(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx, folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <typename ProtocolIn_, typename ProtocolOut_>
  void process_mcGat(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx,folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <class ProtocolIn_, class ProtocolOut_>
  static folly::IOBufQueue return_mcGat(int32_t protoSeqId, apache::thrift::ContextStack* ctx, facebook::memcache::McGatReply const& _return);
  template <class ProtocolIn_, class ProtocolOut_>
  static void throw_wrapped_mcGat(apache::thrift::ResponseChannelRequest::UniquePtr req,int32_t protoSeqId,apache::thrift::ContextStack* ctx,folly::exception_wrapper ew,apache::thrift::Cpp2RequestContext* reqCtx);
  template <typename ProtocolIn_, typename ProtocolOut_>
  void setUpAndProcess_mcGats(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx, folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <typename ProtocolIn_, typename ProtocolOut_>
  void process_mcGats(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx,folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <class ProtocolIn_, class ProtocolOut_>
  static folly::IOBufQueue return_mcGats(int32_t protoSeqId, apache::thrift::ContextStack* ctx, facebook::memcache::McGatsReply const& _return);
  template <class ProtocolIn_, class ProtocolOut_>
  static void throw_wrapped_mcGats(apache::thrift::ResponseChannelRequest::UniquePtr req,int32_t protoSeqId,apache::thrift::ContextStack* ctx,folly::exception_wrapper ew,apache::thrift::Cpp2RequestContext* reqCtx);
  template <typename ProtocolIn_, typename ProtocolOut_>
  void setUpAndProcess_mcGet(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx, folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <typename ProtocolIn_, typename ProtocolOut_>
  void process_mcGet(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx,folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <class ProtocolIn_, class ProtocolOut_>
  static folly::IOBufQueue return_mcGet(int32_t protoSeqId, apache::thrift::ContextStack* ctx, facebook::memcache::McGetReply const& _return);
  template <class ProtocolIn_, class ProtocolOut_>
  static void throw_wrapped_mcGet(apache::thrift::ResponseChannelRequest::UniquePtr req,int32_t protoSeqId,apache::thrift::ContextStack* ctx,folly::exception_wrapper ew,apache::thrift::Cpp2RequestContext* reqCtx);
  template <typename ProtocolIn_, typename ProtocolOut_>
  void setUpAndProcess_mcGets(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx, folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <typename ProtocolIn_, typename ProtocolOut_>
  void process_mcGets(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx,folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <class ProtocolIn_, class ProtocolOut_>
  static folly::IOBufQueue return_mcGets(int32_t protoSeqId, apache::thrift::ContextStack* ctx, facebook::memcache::McGetsReply const& _return);
  template <class ProtocolIn_, class ProtocolOut_>
  static void throw_wrapped_mcGets(apache::thrift::ResponseChannelRequest::UniquePtr req,int32_t protoSeqId,apache::thrift::ContextStack* ctx,folly::exception_wrapper ew,apache::thrift::Cpp2RequestContext* reqCtx);
  template <typename ProtocolIn_, typename ProtocolOut_>
  void setUpAndProcess_mcIncr(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx, folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <typename ProtocolIn_, typename ProtocolOut_>
  void process_mcIncr(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx,folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <class ProtocolIn_, class ProtocolOut_>
  static folly::IOBufQueue return_mcIncr(int32_t protoSeqId, apache::thrift::ContextStack* ctx, facebook::memcache::McIncrReply const& _return);
  template <class ProtocolIn_, class ProtocolOut_>
  static void throw_wrapped_mcIncr(apache::thrift::ResponseChannelRequest::UniquePtr req,int32_t protoSeqId,apache::thrift::ContextStack* ctx,folly::exception_wrapper ew,apache::thrift::Cpp2RequestContext* reqCtx);
  template <typename ProtocolIn_, typename ProtocolOut_>
  void setUpAndProcess_mcLeaseGet(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx, folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <typename ProtocolIn_, typename ProtocolOut_>
  void process_mcLeaseGet(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx,folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <class ProtocolIn_, class ProtocolOut_>
  static folly::IOBufQueue return_mcLeaseGet(int32_t protoSeqId, apache::thrift::ContextStack* ctx, facebook::memcache::McLeaseGetReply const& _return);
  template <class ProtocolIn_, class ProtocolOut_>
  static void throw_wrapped_mcLeaseGet(apache::thrift::ResponseChannelRequest::UniquePtr req,int32_t protoSeqId,apache::thrift::ContextStack* ctx,folly::exception_wrapper ew,apache::thrift::Cpp2RequestContext* reqCtx);
  template <typename ProtocolIn_, typename ProtocolOut_>
  void setUpAndProcess_mcLeaseSet(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx, folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <typename ProtocolIn_, typename ProtocolOut_>
  void process_mcLeaseSet(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx,folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <class ProtocolIn_, class ProtocolOut_>
  static folly::IOBufQueue return_mcLeaseSet(int32_t protoSeqId, apache::thrift::ContextStack* ctx, facebook::memcache::McLeaseSetReply const& _return);
  template <class ProtocolIn_, class ProtocolOut_>
  static void throw_wrapped_mcLeaseSet(apache::thrift::ResponseChannelRequest::UniquePtr req,int32_t protoSeqId,apache::thrift::ContextStack* ctx,folly::exception_wrapper ew,apache::thrift::Cpp2RequestContext* reqCtx);
  template <typename ProtocolIn_, typename ProtocolOut_>
  void setUpAndProcess_mcMetaget(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx, folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <typename ProtocolIn_, typename ProtocolOut_>
  void process_mcMetaget(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx,folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <class ProtocolIn_, class ProtocolOut_>
  static folly::IOBufQueue return_mcMetaget(int32_t protoSeqId, apache::thrift::ContextStack* ctx, facebook::memcache::McMetagetReply const& _return);
  template <class ProtocolIn_, class ProtocolOut_>
  static void throw_wrapped_mcMetaget(apache::thrift::ResponseChannelRequest::UniquePtr req,int32_t protoSeqId,apache::thrift::ContextStack* ctx,folly::exception_wrapper ew,apache::thrift::Cpp2RequestContext* reqCtx);
  template <typename ProtocolIn_, typename ProtocolOut_>
  void setUpAndProcess_mcPrepend(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx, folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <typename ProtocolIn_, typename ProtocolOut_>
  void process_mcPrepend(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx,folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <class ProtocolIn_, class ProtocolOut_>
  static folly::IOBufQueue return_mcPrepend(int32_t protoSeqId, apache::thrift::ContextStack* ctx, facebook::memcache::McPrependReply const& _return);
  template <class ProtocolIn_, class ProtocolOut_>
  static void throw_wrapped_mcPrepend(apache::thrift::ResponseChannelRequest::UniquePtr req,int32_t protoSeqId,apache::thrift::ContextStack* ctx,folly::exception_wrapper ew,apache::thrift::Cpp2RequestContext* reqCtx);
  template <typename ProtocolIn_, typename ProtocolOut_>
  void setUpAndProcess_mcReplace(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx, folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <typename ProtocolIn_, typename ProtocolOut_>
  void process_mcReplace(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx,folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <class ProtocolIn_, class ProtocolOut_>
  static folly::IOBufQueue return_mcReplace(int32_t protoSeqId, apache::thrift::ContextStack* ctx, facebook::memcache::McReplaceReply const& _return);
  template <class ProtocolIn_, class ProtocolOut_>
  static void throw_wrapped_mcReplace(apache::thrift::ResponseChannelRequest::UniquePtr req,int32_t protoSeqId,apache::thrift::ContextStack* ctx,folly::exception_wrapper ew,apache::thrift::Cpp2RequestContext* reqCtx);
  template <typename ProtocolIn_, typename ProtocolOut_>
  void setUpAndProcess_mcSet(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx, folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <typename ProtocolIn_, typename ProtocolOut_>
  void process_mcSet(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx,folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <class ProtocolIn_, class ProtocolOut_>
  static folly::IOBufQueue return_mcSet(int32_t protoSeqId, apache::thrift::ContextStack* ctx, facebook::memcache::McSetReply const& _return);
  template <class ProtocolIn_, class ProtocolOut_>
  static void throw_wrapped_mcSet(apache::thrift::ResponseChannelRequest::UniquePtr req,int32_t protoSeqId,apache::thrift::ContextStack* ctx,folly::exception_wrapper ew,apache::thrift::Cpp2RequestContext* reqCtx);
  template <typename ProtocolIn_, typename ProtocolOut_>
  void setUpAndProcess_mcTouch(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx, folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <typename ProtocolIn_, typename ProtocolOut_>
  void process_mcTouch(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx,folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <class ProtocolIn_, class ProtocolOut_>
  static folly::IOBufQueue return_mcTouch(int32_t protoSeqId, apache::thrift::ContextStack* ctx, facebook::memcache::McTouchReply const& _return);
  template <class ProtocolIn_, class ProtocolOut_>
  static void throw_wrapped_mcTouch(apache::thrift::ResponseChannelRequest::UniquePtr req,int32_t protoSeqId,apache::thrift::ContextStack* ctx,folly::exception_wrapper ew,apache::thrift::Cpp2RequestContext* reqCtx);
  template <typename ProtocolIn_, typename ProtocolOut_>
  void setUpAndProcess_mcVersion(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx, folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <typename ProtocolIn_, typename ProtocolOut_>
  void process_mcVersion(apache::thrift::ResponseChannelRequest::UniquePtr req, apache::thrift::SerializedRequest&& serializedRequest, apache::thrift::Cpp2RequestContext* ctx,folly::EventBase* eb, apache::thrift::concurrency::ThreadManager* tm);
  template <class ProtocolIn_, class ProtocolOut_>
  static folly::IOBufQueue return_mcVersion(int32_t protoSeqId, apache::thrift::ContextStack* ctx, facebook::memcache::McVersionReply const& _return);
  template <class ProtocolIn_, class ProtocolOut_>
  static void throw_wrapped_mcVersion(apache::thrift::ResponseChannelRequest::UniquePtr req,int32_t protoSeqId,apache::thrift::ContextStack* ctx,folly::exception_wrapper ew,apache::thrift::Cpp2RequestContext* reqCtx);
 public:
  MemcacheAsyncProcessor(MemcacheSvIf* iface) :
      iface_(iface) {}

  virtual ~MemcacheAsyncProcessor() {}
};

}}} // facebook::memcache::thrift
