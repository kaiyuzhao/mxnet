/*!
 *  Copyright (c) 2015 by Contributors
 * \file resource.cc
 * \brief Implementation of resource manager.
 */
#include <dmlc/logging.h>
#include <dmlc/parameter.h>
#include <mxnet/base.h>
#include <mxnet/engine.h>
#include <mxnet/resource.h>
#include <limits>
#include <atomic>
#include "./common/lazy_alloc_array.h"

namespace mxnet {
namespace resource {

// implements resource manager
class ResourceManagerImpl : public ResourceManager {
 public:
  ResourceManagerImpl() noexcept(false)
      : global_seed_(0) {
    cpu_temp_space_copy_ = dmlc::GetEnv("MXNET_CPU_TEMP_COPY", 16);
    gpu_temp_space_copy_ = dmlc::GetEnv("MXNET_GPU_TEMP_COPY", 4);
    engine_ref_ = Engine::_GetSharedRef();
    cpu_rand_.reset(new ResourceRandom<cpu>(
        Context::CPU(), global_seed_));
    cpu_space_.reset(new ResourceTempSpace<cpu>(
        Context::CPU(), cpu_temp_space_copy_));
  }
  ~ResourceManagerImpl() {
    // need explicit delete, before engine get killed
    cpu_rand_.reset(nullptr);
    cpu_space_.reset(nullptr);
#if MXNET_USE_CUDA
    gpu_rand_.Clear();
    gpu_space_.Clear();
#endif
    if (engine_ref_ != nullptr) {
      // release the reference to engine.
      engine_ref_ = nullptr;
    }
  }

  // request resources
  Resource Request(Context ctx, const ResourceRequest &req) override {
    if (ctx.dev_mask() == cpu::kDevMask) {
      switch (req.type) {
        case ResourceRequest::kRandom: return cpu_rand_->resource;
        case ResourceRequest::kTempSpace: return cpu_space_->GetNext();
        default: LOG(FATAL) << "Unknown supported type " << req.type;
      }
    } else {
      CHECK_EQ(ctx.dev_mask(), gpu::kDevMask);
#if MSHADOW_USE_CUDA
      switch (req.type) {
        case ResourceRequest::kRandom: {
          return gpu_rand_.Get(ctx.dev_id, [ctx, this]() {
              return new ResourceRandom<gpu>(ctx, global_seed_);
            })->resource;
        }
        case ResourceRequest::kTempSpace: {
          return gpu_space_.Get(ctx.dev_id, [ctx, this]() {
              return new ResourceTempSpace<gpu>(ctx, gpu_temp_space_copy_);
            })->GetNext();
        }
        default: LOG(FATAL) << "Unknown supported type " << req.type;
      }
#else
      LOG(FATAL) << MXNET_GPU_NOT_ENABLED_ERROR;
#endif
    }
    Resource ret;
    return ret;
  }

  void SeedRandom(uint32_t seed) override {
    global_seed_ = seed;
    cpu_rand_->Seed(global_seed_);
#if MXNET_USE_CUDA
    gpu_rand_.ForEach([seed](size_t i, ResourceRandom<gpu> *p) {
        p->Seed(seed);
      });
#endif
  }

 private:
  /*! \brief Maximum number of GPUs */
  static constexpr std::size_t kMaxNumGPUs = 16;
  /*! \brief Random number magic number to seed different random numbers */
  static constexpr uint32_t kRandMagic = 127UL;
  // the random number resources
  template<typename xpu>
  struct ResourceRandom {
    /*! \brief the context of the PRNG */
    Context ctx;
    /*! \brief pointer to PRNG */
    mshadow::Random<xpu> *prnd;
    /*! \brief resource representation */
    Resource resource;
    /*! \brief constructor */
    explicit ResourceRandom(Context ctx, uint32_t global_seed)
        : ctx(ctx) {
      mshadow::SetDevice<xpu>(ctx.dev_id);
      resource.var = Engine::Get()->NewVariable();
      prnd = new mshadow::Random<xpu>(ctx.dev_id + global_seed * kRandMagic);
      resource.ptr_ = prnd;
      resource.req = ResourceRequest(ResourceRequest::kRandom);
    }
    ~ResourceRandom() {
      mshadow::Random<xpu> *r = prnd;
      Engine::Get()->DeleteVariable(
          [r](RunContext rctx) {
            MSHADOW_CATCH_ERROR(delete r);
          }, ctx, resource.var);
    }
    // set seed to a PRNG
    inline void Seed(uint32_t global_seed) {
      uint32_t seed = ctx.dev_id + global_seed * kRandMagic;
      mshadow::Random<xpu> *r = prnd;
      Engine::Get()->PushSync([r, seed](RunContext rctx) {
          r->set_stream(rctx.get_stream<xpu>());
          r->Seed(seed);
        }, ctx, {}, {resource.var});
    }
  };
  // temporal space resource.
  template<typename xpu>
  struct ResourceTempSpace {
    /*! \brief the context of the device */
    Context ctx;
    /*! \brief the underlying space */
    std::vector<mshadow::TensorContainer<xpu, 1, real_t>*> space;
    /*! \brief resource representation */
    std::vector<Resource> resource;
    /*! \brief current pointer to the round roubin alloator */
    std::atomic<size_t> curr_ptr;
    /*! \brief constructor */
    explicit ResourceTempSpace(Context ctx, size_t ncopy)
        : ctx(ctx), space(ncopy), resource(ncopy), curr_ptr(0) {
      mshadow::SetDevice<xpu>(ctx.dev_id);
      for (size_t i = 0; i < space.size(); ++i) {
        space[i] = new mshadow::TensorContainer<xpu, 1, real_t>();
        resource[i].var = Engine::Get()->NewVariable();
        resource[i].id = static_cast<int32_t>(i);
        resource[i].ptr_ = space[i];
        resource[i].req = ResourceRequest(ResourceRequest::kTempSpace);
      }
    }
    ~ResourceTempSpace() {
      for (size_t i = 0; i < space.size(); ++i) {
        mshadow::TensorContainer<xpu, 1, real_t>* r = space[i];
        Engine::Get()->DeleteVariable(
            [r](RunContext rctx){
              MSHADOW_CATCH_ERROR(r->Release());
              delete r;
            }, ctx, resource[i].var);
      }
    }
    // get next resource in round roubin matter
    inline Resource GetNext() {
      const size_t kMaxDigit = std::numeric_limits<size_t>::max() / 2;
      size_t ptr = ++curr_ptr;
      // reset ptr to avoid undefined behavior during overflow
      // usually this won't happen
      if (ptr > kMaxDigit) {
        curr_ptr.store((ptr + 1) % space.size());
      }
      return resource[ptr % space.size()];
    }
  };
  /*! \brief number of copies in CPU temp space */
  int cpu_temp_space_copy_;
  /*! \brief number of copies in GPU temp space */
  int gpu_temp_space_copy_;
  /*! \brief Reference to the engine */
  std::shared_ptr<Engine> engine_ref_;
  /*! \brief internal seed to the random number generator */
  uint32_t global_seed_;
  /*! \brief CPU random number resources */
  std::unique_ptr<ResourceRandom<cpu> > cpu_rand_;
  /*! \brief CPU temp space resources */
  std::unique_ptr<ResourceTempSpace<cpu> > cpu_space_;
#if MXNET_USE_CUDA
  /*! \brief random number generator for GPU */
  common::LazyAllocArray<ResourceRandom<gpu> > gpu_rand_;
  /*! \brief temp space for GPU */
  common::LazyAllocArray<ResourceTempSpace<gpu> > gpu_space_;
#endif
};
}  // namespace resource

static resource::ResourceManagerImpl* instance_ptr = nullptr;
static bool rm_shutdown = false;

ResourceManager* ResourceManager::Get() {
  if (instance_ptr == nullptr) {
    if (rm_shutdown) LOG(FATAL) << "Resource manager already shutdone" << std::endl;
    instance_ptr = new resource::ResourceManagerImpl();
  }
  return instance_ptr;
}

void ResourceManager::Shutdown() {
  if (instance_ptr != nullptr) {
    delete instance_ptr;
    instance_ptr = nullptr;
    rm_shutdown = true;
  }
}
}  // namespace mxnet
