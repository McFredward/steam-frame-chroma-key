#include "../layer/pixel_observation_boundary.h"
#include <cstdlib>
#include <iostream>
#include <system_error>
#include <thread>

namespace {
unsigned checks=0;
void require(bool value){++checks;if(!value)std::exit(1);}
}
int main(){
    frame_pixel_observation::Boundary boundary;
    unsigned observed=0,nativeCalls=0;
    const auto create=[&](bool fail){
        ++nativeCalls;const int nativeResult=17;
        boundary.observe([&]{++observed;if(fail)throw std::system_error(std::make_error_code(std::errc::resource_deadlock_would_occur));});
        return nativeResult;
    };
    require(!boundary.failed());require(create(false)==17 && observed==1 && nativeCalls==1);
    require(create(true)==17 && boundary.failed() && observed==2 && nativeCalls==2);
    require(create(false)==17 && observed==2 && nativeCalls==3);
    bool called=false;
    std::thread later([&]{boundary.observe([&]{called=true;});});later.join();
    require(!called && boundary.failed());
    frame_pixel_observation::Boundary nonstandard;
    nonstandard.observe([]{throw 3;});require(nonstandard.failed());
    std::cout<<"optional observation boundary: "<<checks<<" checks\n";
}
