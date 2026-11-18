#include "sha256.h"

#include <stdio.h>
#include <string.h>

static int check_hex(const unsigned char *digest, const char *hex)
{
    char buf[65];
    int i;

    for (i = 0; i < 32; ++i)
        snprintf(buf + i * 2, 3, "%02x", digest[i]);
    buf[64] = '\0';
    return strcmp(buf, hex) == 0;
}

int main(void)
{
    unsigned char digest[32];
    int failed = 0;

    sha256((const unsigned char *)"abc", 3, digest);
    if (!check_hex(digest,
                   "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")) {
        fputs("sha256 vector failed\n", stderr);
        failed = 1;
    }

    hmac_sha256((const unsigned char *)"key", 3,
                (const unsigned char *)
                "The quick brown fox jumps over the lazy dog", 43, digest);
    if (!check_hex(digest,
                   "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8")) {
        fputs("hmac-sha256 vector failed\n", stderr);
        failed = 1;
    }

    if (!failed)
        puts("SHA-256/HMAC tests passed");
    return failed;
}
