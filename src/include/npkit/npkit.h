#ifndef NPKIT_H_
#define NPKIT_H_

#include <string>
#include <thread>

#include <cuda_runtime.h>

#include "npkit/npkit_event.h"
#include "npkit/npkit_struct.h"

class NpKit {
 public:
  static const uint64_t kNumGpuEventBuffers = 512;

  static const uint64_t kNumCpuEventBuffers = 32;

  static ncclResult_t Init(int rank);

  static ncclResult_t Dump(const std::string& dump_dir);

  static ncclResult_t Shutdown();

  static NpKitEventCollectContext* GetGpuEventCollectContexts();

  static inline __device__ void CollectGpuEvent(uint8_t type, uint32_t size, uint32_t rsvd, uint64_t timestamp,
                                                NpKitEventCollectContext* ctx) {
    uint64_t event_buffer_head = ctx->event_buffer_head;
    if (event_buffer_head < kMaxNumGpuEventsPerBuffer) {
      NpKitEvent& event = ctx->event_buffer[event_buffer_head];
      event.fields.type = type;
      event.fields.size = size;
      event.fields.rsvd = rsvd;
      event.fields.timestamp = timestamp;
      ctx->event_buffer_head++;
    }
  }

  static void CollectCpuEvent(uint8_t type, uint32_t size, uint32_t rsvd, uint64_t timestamp, int channel_id);

  static uint64_t* GetCpuTimestamp();

 private:
  static void CpuTimestampUpdateThread();

  // 64K * 512 * 16B = 512MB per GPU
  static const uint64_t kMaxNumGpuEventsPerBuffer = 1ULL << 16;

  // 64K * 2 (send/recv) * (512/32) = 2M, 2M * 32 * 16B = 1GB per CPU
  static const uint64_t kMaxNumCpuEventsPerBuffer = 1ULL << 21;

  static NpKitEvent** gpu_event_buffers_;
  static NpKitEvent** cpu_event_buffers_;

  static NpKitEventCollectContext* gpu_collect_contexts_;
  static NpKitEventCollectContext* cpu_collect_contexts_;
  static uint64_t* cpu_timestamp_;

  static uint64_t rank_;

  static std::thread* cpu_timestamp_update_thread_;
  static volatile bool cpu_timestamp_update_thread_should_stop_;
};

#if defined(ENABLE_NPKIT_GPU_EVENTS)

#define NPKIT_GPU_SET_CTX_ID(__ctx_id__, __thread_flag__) \
  if (__thread_flag__) { \
    prims.npKitCtxIdx = __ctx_id__; \
  }

#define NPKIT_GPU_TREE_SPLIT_DECL_CTX_ID_AND_THREAD_FLAG() \
  bool isNpKitThread = false; \
  int npKitCtxIdx = 0; \
  if (threadIdx.x == 0) { \
    isNpKitThread = true; \
    npKitCtxIdx = bid * 2; \
  } else if (tree->up != -1 && threadIdx.x == nthreadsSplit) { \
    isNpKitThread = true; \
    npKitCtxIdx = bid * 2 + 1; \
  }

#define NPKIT_GPU_SEND_DECL_CTX_ID() \
  int npKitCtxIdx = blockIdx.x * NCCL_MAX_WORK_ELEMENTS_P2P;

#define NPKIT_GPU_RECV_DECL_CTX_ID() \
  int npKitCtxIdx = blockIdx.x * NCCL_MAX_WORK_ELEMENTS_P2P + 1;

#define NPKIT_GPU_SYNC_TIME(__ctx_id__, __thread_flag__) \
  if (__thread_flag__) { \
    NpKit::CollectGpuEvent(NPKIT_EVENT_TIME_SYNC_CPU, 0, 0, *(ncclShmem.comm.npKitCpuTimestamp), \
        ncclShmem.comm.npKitEventCollectContexts + __ctx_id__); \
    NpKit::CollectGpuEvent(NPKIT_EVENT_TIME_SYNC_GPU, 0, 0, clock64(), \
        ncclShmem.comm.npKitEventCollectContexts + __ctx_id__); \
  }

#define NPKIT_GPU_COLLECT_EVENT(__ctx_id__, __type__, __size__, __rsvd__) \
  if (tid == 0) { \
    NpKit::CollectGpuEvent(__type__, __size__, __rsvd__, clock64(), \
        ncclShmem.comm.npKitEventCollectContexts + __ctx_id__); \
  }

#define NPKIT_GPU_PRIMS_DECL_FIELDS() \
  public: \
    int npKitCtxIdx = 0; \
  private: \
    uint64_t npKitWaitEntryTime = 0; \
    uint64_t npKitWaitExitTime = 0; \
    uint64_t npKitWaitTotalTime = 0;

#define NPKIT_GPU_PRIMS_OP_INIT() \
  if (tid == 0) { \
    npKitWaitTotalTime = 0; \
  }

#define NPKIT_GPU_PRIMS_WAIT_BEGIN() \
  if (tid == 0) { \
    npKitWaitEntryTime = clock64(); \
  }

#define NPKIT_GPU_PRIMS_WAIT_END() \
  if (tid == 0) { \
    npKitWaitExitTime = clock64(); \
    npKitWaitTotalTime += npKitWaitExitTime - npKitWaitEntryTime; \
  }

#define NPKIT_GPU_PRIMS_WAIT_BEGIN_WITH_SPIN() \
  int npKitWaitSpins = 0; \
  if (tid == 0) { \
    npKitWaitEntryTime = clock64(); \
  }

#define NPKIT_GPU_PRIMS_WAIT_INC_SPIN() \
  npKitWaitSpins++;

#define NPKIT_GPU_PRIMS_WAIT_END_WITH_SPIN() \
  if (tid == 0) { \
    npKitWaitExitTime = clock64(); \
    npKitWaitTotalTime += (npKitWaitExitTime - npKitWaitEntryTime) * (npKitWaitSpins - 1) / npKitWaitSpins; \
  }

#else

#define NPKIT_GPU_SET_CTX_ID(__ctx_id__, __thread_flag__)

#define NPKIT_GPU_TREE_SPLIT_DECL_CTX_ID_AND_THREAD_FLAG()

#define NPKIT_GPU_SEND_DECL_CTX_ID()

#define NPKIT_GPU_RECV_DECL_CTX_ID()

#define NPKIT_GPU_SYNC_TIME(__ctx_id__, __thread_flag__)

#define NPKIT_GPU_COLLECT_EVENT(__ctx_id__, __type__, __size__, __rsvd__)

#define NPKIT_GPU_PRIMS_DECL_FIELDS()

#define NPKIT_GPU_PRIMS_OP_INIT()

#define NPKIT_GPU_PRIMS_WAIT_BEGIN()

#define NPKIT_GPU_PRIMS_WAIT_END()

#define NPKIT_GPU_PRIMS_WAIT_BEGIN_WITH_SPIN()

#define NPKIT_GPU_PRIMS_WAIT_INC_SPIN()

#define NPKIT_GPU_PRIMS_WAIT_END_WITH_SPIN()

#endif

#if defined(ENABLE_NPKIT_CPU_EVENTS)

#define NPKIT_CPU_COLLECT_EVENT(__ctx_id__, __type__, __size__, __rsvd__) \
  NpKit::CollectCpuEvent(__type__, __size__, __rsvd__, \
      *(volatile uint64_t*)NpKit::GetCpuTimestamp(), __ctx_id__); \

#define NPKIT_CPU_PROXY_SAVE_SIZE() \
  sub->npKitSizesFifo[buffSlot] = size;

#else

#define NPKIT_CPU_COLLECT_EVENT(__ctx_id__, __type__, __size__, __rsvd__)

#define NPKIT_CPU_PROXY_SAVE_SIZE()

#endif

#if defined(ENABLE_NPKIT_CPU_EVENTS) || defined(ENABLE_NPKIT_GPU_EVENTS)

#define NPKIT_INIT() \
  NCCLCHECK(NpKit::Init(comm->rank)); \
  comm->hostDevComm.npKitEventCollectContexts = NpKit::GetGpuEventCollectContexts(); \
  comm->hostDevComm.npKitCpuTimestamp = NpKit::GetCpuTimestamp();

#define NPKIT_TEARDOWN() \
  const char* npKitDumpDir = getenv("NPKIT_DUMP_DIR"); \
  if (npKitDumpDir == nullptr) { \
    WARN("NPKIT_DUMP_DIR is empty"); \
  } else { \
    NCCLCHECK(NpKit::Dump(npKitDumpDir)); \
  } \
  NCCLCHECK(NpKit::Shutdown());

#else

#define NPKIT_INIT()

#define NPKIT_TEARDOWN()

#endif

#endif
