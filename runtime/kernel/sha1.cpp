#include "sha1.h"

#include <algorithm>
#include <cstring>

namespace
{
inline uint32_t Rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }
}   // namespace

Sha1::Sha1() : m_h{ 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 } {}

void Sha1::Block(const uint8_t* p)
{
    uint32_t w[80];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 80; i++) w[i] = Rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = m_h[0], b = m_h[1], c = m_h[2], d = m_h[3], e = m_h[4];
    for (int i = 0; i < 80; i++)
    {
        uint32_t f, k;
        if (i < 20) f = (b & c) | (~b & d), k = 0x5A827999;
        else if (i < 40) f = b ^ c ^ d, k = 0x6ED9EBA1;
        else if (i < 60) f = (b & c) | (b & d) | (c & d), k = 0x8F1BBCDC;
        else f = b ^ c ^ d, k = 0xCA62C1D6;
        uint32_t t = Rol(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = Rol(b, 30);
        b = a;
        a = t;
    }
    m_h[0] += a;
    m_h[1] += b;
    m_h[2] += c;
    m_h[3] += d;
    m_h[4] += e;
}

void Sha1::Update(const void* data, size_t len)
{
    const uint8_t* p = (const uint8_t*)data;
    m_total += len;
    if (m_bufLen)
    {
        size_t n = std::min(len, 64 - m_bufLen);
        memcpy(m_buf + m_bufLen, p, n);
        m_bufLen += n;
        p += n;
        len -= n;
        if (m_bufLen < 64) return;
        Block(m_buf);
        m_bufLen = 0;
    }
    for (; len >= 64; p += 64, len -= 64) Block(p);
    memcpy(m_buf, p, len);
    m_bufLen = len;
}

void Sha1::Final(uint8_t out[20])
{
    uint64_t bits = m_total * 8;
    uint8_t pad = 0x80;
    Update(&pad, 1);
    uint8_t zero = 0;
    while (m_bufLen != 56) Update(&zero, 1);
    uint8_t len[8];
    for (int i = 0; i < 8; i++) len[i] = (uint8_t)(bits >> (56 - 8 * i));
    Update(len, 8);
    for (int i = 0; i < 5; i++)
        for (int j = 0; j < 4; j++) out[4 * i + j] = (uint8_t)(m_h[i] >> (24 - 8 * j));
}

void Sha1::Hash(const void* data, size_t len, uint8_t out[20])
{
    Sha1 s;
    s.Update(data, len);
    s.Final(out);
}
