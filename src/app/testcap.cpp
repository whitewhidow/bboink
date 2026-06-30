// testcap.cpp — generate a REAL, crackable WPA2 handshake for testing wpa-sec /
// OnlineHashCrack. Computes a genuine 4-way handshake MIC from a known password
// using mbedTLS (PBKDF2 -> PTK -> HMAC-SHA1), then writes a hashcat .22000 file
// (for OHC) and a radiotap .pcap (beacon + M1 + M2, for wpa-sec). Password is a
// common one ("password123") so the cloud wordlists actually crack it.
#include "testcap.h"
#include "../core/storage.h"
#include "../core/config.h"
#include "../core/sd_layout.h"
#include <FS.h>
#include <esp_random.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/md.h>

namespace {

const char* PASSWORD = "password123";   // common -> crackable by cloud wordlists

void hmacSha1(const uint8_t* key, size_t kl, const uint8_t* data, size_t dl, uint8_t out[20]) {
    const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    mbedtls_md_hmac(md, key, kl, data, dl, out);
}

// IEEE 802.11 PRF: produce outlen bytes from PRF(K, label, B).
void wpaPrf(const uint8_t* k, size_t kl, const char* label, const uint8_t* b, size_t bl,
            uint8_t* out, size_t outlen) {
    uint8_t buf[128];
    size_t ll = strlen(label);
    uint8_t i = 0, digest[20];
    size_t pos = 0;
    while (pos < outlen) {
        size_t p = 0;
        memcpy(buf + p, label, ll); p += ll;
        buf[p++] = 0x00;
        memcpy(buf + p, b, bl); p += bl;
        buf[p++] = i;
        hmacSha1(k, kl, buf, p, digest);
        size_t n = (outlen - pos < 20) ? (outlen - pos) : 20;
        memcpy(out + pos, digest, n);
        pos += n; i++;
    }
}

void toHex(const uint8_t* in, size_t len, char* out) {
    static const char* H = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) { out[i*2] = H[in[i] >> 4]; out[i*2+1] = H[in[i] & 0xf]; }
    out[len*2] = '\0';
}

// Build an EAPOL-Key frame. msg=2 -> M2 (from STA, with MIC), msg=1 -> M1 (from AP).
// `nonce` is SNonce (M2) or ANonce (M1). Returns frame length. MIC field left zero.
size_t buildEapol(uint8_t* e, int msg, const uint8_t* nonce, uint8_t replayLo) {
    const uint8_t rsnie[] = {0x30,0x14,0x01,0x00,0x00,0x0f,0xac,0x04,0x01,0x00,
                             0x00,0x0f,0xac,0x04,0x01,0x00,0x00,0x0f,0xac,0x02,0x00,0x00};
    size_t kd = (msg == 2) ? sizeof(rsnie) : 0;
    size_t descLen = 1+2+2+8+32+16+8+8+16+2 + kd;
    size_t n = 0;
    e[n++] = 0x02; e[n++] = 0x03;                       // version, EAPOL-Key
    e[n++] = (descLen >> 8) & 0xff; e[n++] = descLen & 0xff;
    e[n++] = 0x02;                                       // RSN descriptor
    if (msg == 2) { e[n++] = 0x01; e[n++] = 0x0a; }      // key info M2 (pairwise+mic, v2)
    else          { e[n++] = 0x00; e[n++] = 0x8a; }      // key info M1 (pairwise+ack, v2)
    e[n++] = 0x00; e[n++] = 0x00;                        // key length
    for (int i = 0; i < 7; i++) e[n++] = 0x00; e[n++] = replayLo;  // replay counter
    memcpy(e + n, nonce, 32); n += 32;                   // key nonce
    memset(e + n, 0, 16); n += 16;                       // key IV
    memset(e + n, 0, 8);  n += 8;                        // key RSC
    memset(e + n, 0, 8);  n += 8;                        // key ID
    memset(e + n, 0, 16); n += 16;                       // MIC (zero)
    e[n++] = (kd >> 8) & 0xff; e[n++] = kd & 0xff;       // key data length
    if (kd) { memcpy(e + n, rsnie, kd); n += kd; }
    return n;
}

// --- minimal radiotap pcap writers ---
void le32(File& f, uint32_t v) { uint8_t b[4]={(uint8_t)v,(uint8_t)(v>>8),(uint8_t)(v>>16),(uint8_t)(v>>24)}; f.write(b,4); }
void le16(File& f, uint16_t v) { uint8_t b[2]={(uint8_t)v,(uint8_t)(v>>8)}; f.write(b,2); }
void pcapHdr(File& f) { le32(f,0xA1B2C3D4); le16(f,2); le16(f,4); le32(f,0); le32(f,0); le32(f,65535); le32(f,127); }
void pcapPkt(File& f, const uint8_t* d, uint16_t len, uint32_t ts) {
    static const uint8_t rt[8] = {0,0,8,0,0,0,0,0};
    le32(f, ts); le32(f, 0); le32(f, 8 + len); le32(f, 8 + len);
    f.write(rt, 8); f.write(d, len);
}

const uint8_t BCAST[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

} // namespace

namespace TestCap {

bool generate(char* outPath, size_t outLen) {
    if (!Config::isSDAvailable()) return false;

    // 1. SSID + identities + nonces
    char ssid[24];
    snprintf(ssid, sizeof(ssid), "TestNet_%04X", (unsigned)(esp_random() & 0xFFFF));
    uint8_t ap[6], sta[6], anonce[32], snonce[32];
    esp_fill_random(ap, 6);  ap[0]  = (ap[0]  & 0xFE) | 0x02;
    esp_fill_random(sta, 6); sta[0] = (sta[0] & 0xFE) | 0x02;
    esp_fill_random(anonce, 32);
    esp_fill_random(snonce, 32);

    // 2. PMK = PBKDF2-SHA1(password, ssid, 4096, 32)
    uint8_t pmk[32];
    {
        const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
        mbedtls_md_context_t ctx; mbedtls_md_init(&ctx);
        mbedtls_md_setup(&ctx, md, 1);
        mbedtls_pkcs5_pbkdf2_hmac(&ctx, (const uint8_t*)PASSWORD, strlen(PASSWORD),
                                  (const uint8_t*)ssid, strlen(ssid), 4096, 32, pmk);
        mbedtls_md_free(&ctx);
    }

    // 3. PTK -> KCK (first 16 bytes). B = min|max(MAC) || min|max(nonce)
    uint8_t b[76];
    if (memcmp(ap, sta, 6) < 0) { memcpy(b, ap, 6); memcpy(b+6, sta, 6); }
    else                        { memcpy(b, sta, 6); memcpy(b+6, ap, 6); }
    if (memcmp(anonce, snonce, 32) < 0) { memcpy(b+12, anonce, 32); memcpy(b+44, snonce, 32); }
    else                                { memcpy(b+12, snonce, 32); memcpy(b+44, anonce, 32); }
    uint8_t ptk[64];
    wpaPrf(pmk, 32, "Pairwise key expansion", b, 76, ptk, 64);

    // 4. M2 EAPOL + MIC = HMAC-SHA1(KCK, M2)[0:16]
    uint8_t m2[160]; size_t m2len = buildEapol(m2, 2, snonce, 0x01);
    uint8_t digest[20]; hmacSha1(ptk, 16, m2, m2len, digest);
    uint8_t mic[16]; memcpy(mic, digest, 16);

    // 5. write .22000 : WPA*02*MIC*AP*STA*ESSID*ANONCE*EAPOL*00
    char micHex[33], apHex[13], staHex[13], essidHex[52], anonceHex[65], eapolHex[400];
    toHex(mic,16,micHex); toHex(ap,6,apHex); toHex(sta,6,staHex);
    toHex((const uint8_t*)ssid, strlen(ssid), essidHex);
    toHex(anonce,32,anonceHex); toHex(m2,m2len,eapolHex);

    char path[100];
    snprintf(path, sizeof(path), "%s/%s_%s_22000.22000", SDLayout::handshakesDir(), ssid, apHex);
    File f = Storage::fs().open(path, FILE_WRITE);
    if (!f) return false;
    f.printf("WPA*02*%s*%s*%s*%s*%s*%s*00\n", micHex, apHex, staHex, essidHex, anonceHex, eapolHex);
    f.close();

    // The .22000 line above needs the EAPOL with a ZEROED MIC field (hashcat
    // convention — the MIC is carried separately as micHex). The .pcap is the
    // opposite: hcxpcapngtool (which wpa-sec runs server-side) rejects an
    // all-zero MIC as a malformed M2 and writes NO hash, so wpa-sec would never
    // see the network. Inject the real MIC into the M2 frame for the pcap. The
    // MIC field is at offset 81 in the EAPOL-Key frame:
    //   eapolhdr(4)+desc(1)+keyinfo(2)+keylen(2)+replay(8)+nonce(32)+iv(16)
    //   +rsc(8)+reserved(8) = 81.
    static constexpr size_t EAPOL_MIC_OFFSET = 81;
    memcpy(m2 + EAPOL_MIC_OFFSET, mic, 16);

    // 6. write matching .pcap (beacon + M1 + M2) for wpa-sec
    char ppath[100];
    snprintf(ppath, sizeof(ppath), "%s/%s_%s_pcap.pcap", SDLayout::handshakesDir(), ssid, apHex);
    File p = Storage::fs().open(ppath, FILE_WRITE);
    if (p) {
        pcapHdr(p);
        // beacon
        uint8_t bc[128]; size_t bn = 0;
        bc[bn++]=0x80; bc[bn++]=0x00; bc[bn++]=0; bc[bn++]=0;
        memcpy(bc+bn,BCAST,6); bn+=6; memcpy(bc+bn,ap,6); bn+=6; memcpy(bc+bn,ap,6); bn+=6;
        bc[bn++]=0; bc[bn++]=0;
        memset(bc+bn,0,8); bn+=8; bc[bn++]=0x64; bc[bn++]=0x00; bc[bn++]=0x01; bc[bn++]=0x04;
        bc[bn++]=0x00; bc[bn++]=strlen(ssid); memcpy(bc+bn,ssid,strlen(ssid)); bn+=strlen(ssid);
        static const uint8_t rates[8] = {0x82,0x84,0x8b,0x96,0x24,0x30,0x48,0x6c};
        bc[bn++]=0x01; bc[bn++]=0x08; for (int ri=0; ri<8; ri++) bc[bn++]=rates[ri];
        bc[bn++]=0x03; bc[bn++]=0x01; bc[bn++]=0x06;
        pcapPkt(p, bc, bn, 1700000000u);
        // EAPOL data frame wrapper: 24B 802.11 + LLC + eapol
        auto writeData = [&](bool fromAp, const uint8_t* eapol, size_t el) {
            uint8_t fr[300]; size_t n=0;
            fr[n++]=0x08; fr[n++]= fromAp ? 0x02 : 0x01;  // data, fromDS/toDS
            fr[n++]=0; fr[n++]=0;
            if (fromAp){ memcpy(fr+n,sta,6); n+=6; memcpy(fr+n,ap,6); n+=6; }
            else       { memcpy(fr+n,ap,6); n+=6; memcpy(fr+n,sta,6); n+=6; }
            memcpy(fr+n,ap,6); n+=6; fr[n++]=0; fr[n++]=0;
            const uint8_t llc[8]={0xAA,0xAA,0x03,0x00,0x00,0x00,0x88,0x8E};
            memcpy(fr+n,llc,8); n+=8;
            memcpy(fr+n,eapol,el); n+=el;
            pcapPkt(p, fr, n, 1700000001u);
        };
        uint8_t m1[160]; size_t m1len = buildEapol(m1, 1, anonce, 0x01);
        writeData(true,  m1, m1len);   // M1 AP->STA
        writeData(false, m2, m2len);   // M2 STA->AP (carries MIC)
        p.close();
    }

    if (outPath && outLen) { strncpy(outPath, path, outLen-1); outPath[outLen-1]='\0'; }
    return true;
}

} // namespace TestCap
