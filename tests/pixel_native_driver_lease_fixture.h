#pragma once
// Synthetic callback-permission fixture ONLY. This does not verify any native
// provider and must never be used as hardware/provider identity evidence.
#ifndef FRAME_PIXEL_NATIVE_DRIVER_LEASE_TEST
#error "This synthetic fixture is restricted to the explicit lease test target"
#endif
#include "../layer/pixel_native_driver_lease.h"
namespace frame_pixel_native_driver {
struct LeaseTestAccess {
    static void advanceEpoch(Lease& lease) noexcept { lease.epoch_.fetch_add(1,std::memory_order_acq_rel); }
    static bool fixture(Lease& lease,const Inputs& inputs) noexcept {
        if(lease.state_.load()!=State::Empty || !inputs.device) return false;
        lease.inputs_=inputs; lease.epoch_.store(1);
        const PFN_vkVoidFunction routes[]{inputs.begin2,inputs.begin2KHR,inputs.next2,inputs.next2KHR};
        for(unsigned i=0;i<4;++i) if(routes[i]) lease.routeMask_|=1u<<i;
        lease.published_.store(true,std::memory_order_release);
        lease.state_.store(State::Ready,std::memory_order_release); return true;
    }
};
}
