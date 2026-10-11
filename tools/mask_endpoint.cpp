// Diagnostic transport for explicit R8 masks. This does NOT capture or identify
// Steam Link video frames; the expected identity is supplied by the test caller.
#include "mask/mask_protocol.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
using Socket = SOCKET;
using SocketLength = int;
static constexpr Socket invalidSocket = INVALID_SOCKET;
static void closeSocket(Socket socket) { closesocket(socket); }
struct Network {
    Network() { WSADATA data{}; if (WSAStartup(MAKEWORD(2, 2), &data)) throw std::runtime_error("WSAStartup failed"); }
    ~Network() { WSACleanup(); }
};
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using Socket = int;
using SocketLength = socklen_t;
static constexpr Socket invalidSocket = -1;
static void closeSocket(Socket socket) { close(socket); }
struct Network {};
#endif

struct UdpSocket {
    Socket value = socket(AF_INET, SOCK_DGRAM, 0);
    UdpSocket() { if (value == invalidSocket) throw std::runtime_error("Cannot create UDP socket"); }
    ~UdpSocket() { if (value != invalidSocket) closeSocket(value); }
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
};

static uint64_t number(const char *text, uint64_t maximum) {
    const std::string value(text);
    uint64_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || result > maximum)
        throw std::invalid_argument("Invalid numeric argument: " + value);
    return result;
}

static mr::mask::SessionId session(const char *text) {
    const std::string value(text);
    if (value.size() != 32) throw std::invalid_argument("Session must contain exactly 32 hexadecimal digits");
    mr::mask::SessionId result{};
    for (size_t index = 0; index < result.size(); ++index) {
        unsigned parsed = 0;
        const auto conversion = std::from_chars(value.data() + 2 * index, value.data() + 2 * index + 2, parsed, 16);
        if (conversion.ec != std::errc{} || conversion.ptr != value.data() + 2 * index + 2)
            throw std::invalid_argument("Invalid hexadecimal session");
        result[index] = static_cast<uint8_t>(parsed);
    }
    if (std::all_of(result.begin(), result.end(), [](uint8_t byte) { return byte == 0; }))
        throw std::invalid_argument("Session must not be all zero");
    return result;
}

static sockaddr_in address(const char *host, const char *port, bool bindAddress) {
    const auto parsedPort = number(port, 65535);
    if (!bindAddress && !parsedPort) throw std::invalid_argument("Destination port must not be zero");
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo *results = nullptr;
    if (getaddrinfo(host, nullptr, &hints, &results) != 0 || !results)
        throw std::invalid_argument("Cannot resolve IPv4 address");
    const auto resolved = *reinterpret_cast<sockaddr_in *>(results->ai_addr);
    freeaddrinfo(results);
    auto result = resolved;
    result.sin_port = htons(static_cast<uint16_t>(parsedPort));
    return result;
}

static std::string token(std::istream &stream) {
    std::string value;
    for (;;) {
        stream >> std::ws;
        if (stream.peek() != '#') break;
        // Bound comments as well as image data; malicious inputs must not grow memory.
        char character = 0;
        for (size_t index = 0; index < 4096 && stream.get(character); ++index) {
            if (character == '\n') break;
            if (index == 4095) throw std::invalid_argument("PGM comment too long");
        }
    }
    char character = 0;
    while (stream.get(character)) {
        if (character == ' ' || character == '\n' || character == '\t' || character == '\r') {
            if (character == '\r' && stream.peek() == '\n') stream.get();
            break;
        }
        value += character;
        if (value.size() > 16) throw std::invalid_argument("PGM token too long");
    }
    if (value.empty()) throw std::invalid_argument("Missing PGM token");
    return value;
}

static std::vector<uint8_t> readPgm(const char *path, mr::mask::FrameIdentity &identity) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream || token(stream) != "P5") throw std::invalid_argument("Expected binary P5 PGM mask");
    identity.width = static_cast<uint16_t>(number(token(stream).c_str(), 4096));
    identity.height = static_cast<uint16_t>(number(token(stream).c_str(), 4096));
    if (!identity.width || !identity.height || token(stream) != "255")
        throw std::invalid_argument("Expected nonempty R8 PGM with maxval 255");
    std::vector<uint8_t> coverage(static_cast<size_t>(identity.width) * identity.height);
    if (!stream.read(reinterpret_cast<char *>(coverage.data()), static_cast<std::streamsize>(coverage.size())) ||
        stream.peek() != std::char_traits<char>::eof()) throw std::invalid_argument("PGM data size mismatch");
    return coverage;
}

static uint64_t nowMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

static int sendMask(char **arguments) {
    UdpSocket socket;
    const auto destination = address(arguments[2], arguments[3], false);
    mr::mask::FrameIdentity identity{};
    identity.session = session(arguments[4]);
    identity.frame = number(arguments[5], std::numeric_limits<uint64_t>::max());
    identity.eye = static_cast<uint8_t>(number(arguments[6], 1));
    const auto coverage = readPgm(arguments[7], identity);
    const auto packets = mr::mask::encode(identity, coverage);
    for (const auto &packet : packets) {
        const auto sent = sendto(socket.value, reinterpret_cast<const char *>(packet.data()),
            static_cast<int>(packet.size()), 0, reinterpret_cast<const sockaddr *>(&destination), sizeof(destination));
        if (sent < 0 || static_cast<size_t>(sent) != packet.size()) throw std::runtime_error("UDP send failed");
    }
    std::cout << "Sent diagnostic mask frame=" << identity.frame << " eye=" << unsigned(identity.eye)
              << " extent=" << identity.width << 'x' << identity.height << " packets=" << packets.size() << '\n';
    return 0;
}

static int receiveMask(int argc, char **arguments) {
    UdpSocket socket;
    const auto endpoint = address(arguments[2], arguments[3], true);
    if (bind(socket.value, reinterpret_cast<const sockaddr *>(&endpoint), sizeof(endpoint)))
        throw std::runtime_error("Cannot bind UDP mask receiver");
    sockaddr_in actual{};
    SocketLength length = sizeof(actual);
    if (getsockname(socket.value, reinterpret_cast<sockaddr *>(&actual), &length))
        throw std::runtime_error("Cannot query bound port");
    mr::mask::FrameIdentity expected{};
    expected.session = session(arguments[4]);
    expected.frame = number(arguments[5], std::numeric_limits<uint64_t>::max());
    expected.eye = static_cast<uint8_t>(number(arguments[6], 1));
    expected.width = static_cast<uint16_t>(number(arguments[7], 4096));
    expected.height = static_cast<uint16_t>(number(arguments[8], 4096));
    if (!expected.width || !expected.height) throw std::invalid_argument("Extent must not be zero");
    const uint64_t timeout = argc == 11 ? number(arguments[10], 60000) : 2000;
    if (!timeout) throw std::invalid_argument("Timeout must not be zero");
    mr::mask::Receiver receiver;
    receiver.beginSession(expected.session);
    std::cout << "READY port=" << ntohs(actual.sin_port) << " diagnostic-only\n" << std::flush;
    const uint64_t deadline = nowMs() + timeout;
    std::array<uint8_t, mr::mask::kMaxPacketBytes + 1> bytes{};
    while (nowMs() < deadline) {
        fd_set readers;
        FD_ZERO(&readers);
        FD_SET(socket.value, &readers);
        timeval wait{0, 10000};
        const int ready = select(static_cast<int>(socket.value + 1), &readers, nullptr, nullptr, &wait);
        if (ready < 0) throw std::runtime_error("UDP receive wait failed");
        if (!ready) { receiver.expire(nowMs()); continue; }
        const auto size = recvfrom(socket.value, reinterpret_cast<char *>(bytes.data()),
            static_cast<int>(bytes.size()), 0, nullptr, nullptr);
        if (size < 0) throw std::runtime_error("UDP receive failed");
        receiver.ingest(bytes.data(), static_cast<size_t>(size), nowMs());
        const auto coverage = receiver.takeExact(expected, nowMs());
        if (!coverage) continue;
        std::ofstream output(arguments[9], std::ios::binary | std::ios::trunc);
        output << "P5\n" << expected.width << ' ' << expected.height << "\n255\n";
        output.write(reinterpret_cast<const char *>(coverage->data()), static_cast<std::streamsize>(coverage->size()));
        output.flush();
        if (!output) throw std::runtime_error("Cannot write matched mask");
        std::cout << "Matched diagnostic identity frame=" << expected.frame << " eye=" << unsigned(expected.eye)
                  << "; no Steam Link color frame was identified\n";
        return 0;
    }
    std::cerr << "No exact diagnostic mask match before deadline; live rendering must remain opaque\n";
    return 3;
}

int main(int argc, char **argv) {
    try {
        Network network;
        (void)network;
        if (argc == 8 && std::string(argv[1]) == "send") return sendMask(argv);
        if ((argc == 10 || argc == 11) && std::string(argv[1]) == "receive") return receiveMask(argc, argv);
        std::cerr << "Diagnostic only; this tool does not obtain video frame identities.\n"
                  << "mask_endpoint send HOST PORT SESSION_HEX FRAME EYE MASK.pgm\n"
                  << "mask_endpoint receive BIND PORT SESSION_HEX FRAME EYE WIDTH HEIGHT OUT.pgm [TIMEOUT_MS]\n"
                  << "Bind to 127.0.0.1 for local tests; remote use needs a trusted/authenticated transport.\n";
        return 2;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
