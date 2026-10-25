#pragma once
// SHA-1 (FIPS 180-4) for checking installed game files against the known hashes.
#include <cstddef>
#include <cstdint>

class Sha1
{
public:
    Sha1();
    void Update(const void* data, size_t len);
    void Final(uint8_t out[20]);
    static void Hash(const void* data, size_t len, uint8_t out[20]);

private:
    void Block(const uint8_t* p);
    uint32_t m_h[5];
    uint8_t m_buf[64];
    size_t m_bufLen = 0;
    uint64_t m_total = 0;
};
