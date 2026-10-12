#pragma once

// One existing native-runtime TLS byte, with independent historical claims.
// A fresh thread starts at zero even if its TLS address was used before.
namespace frame_client_thread {
constexpr unsigned char kConnection=1,kResource=2,kStartup=4;
inline void claim(unsigned char& marker,unsigned char bit) noexcept {marker|=bit;}
inline bool claimed(unsigned char marker,unsigned char bit) noexcept {return (marker&bit)!=0;}
inline bool startup_stack_owned(unsigned char marker,bool current) noexcept {
    return claimed(marker,kStartup) && current;
}
} // namespace frame_client_thread
