// Backwards compatibility of saved seeds.
// "Seed N" must keep growing exactly the same patch in every later version, otherwise songs that recall a seed would change.
// The fingerprints below were recorded from the first released version (48 parameters, before mono/glide were added).
// New parameters must be APPENDED to the parameter table and never consume random numbers ahead of the old ones.
//   g++ -std=c++17 -I Source Tests/seed_compat_test.cpp -o seed_compat_test && ./seed_compat_test
#include "Params.h"
#include "Randomizer.h"
#include <cstdio>
#include <cstring>
using namespace seed;

struct Golden { uint32_t seed; float chaos; int tune; unsigned long long hash; };
static const Golden kGolden[] =
{
    { 1u, 0.00f, 0, 0xcccf8b0477480e9aull },
    { 1u, 0.00f, 1, 0x35c98b5e22234a89ull },
    { 1u, 0.15f, 0, 0x8dd72ca5b454bc1aull },
    { 1u, 0.15f, 1, 0x22222cf283f3b4f8ull },
    { 1u, 0.20f, 0, 0x7ca3cad15d744d20ull },
    { 1u, 0.20f, 1, 0x11f7358343592a03ull },
    { 1u, 0.50f, 0, 0x3921ff4fab749702ull },
    { 1u, 0.50f, 1, 0x2f328768c9c0e28full },
    { 1u, 1.00f, 0, 0x3b1e764073654019ull },
    { 1u, 1.00f, 1, 0x2ee906e877253f6dull },
    { 29u, 0.00f, 0, 0x14f672e6993464bbull },
    { 29u, 0.00f, 1, 0xbbcf619c8e4400dbull },
    { 29u, 0.15f, 0, 0xce820bd0621257d1ull },
    { 29u, 0.15f, 1, 0x1ed06c3969aaf619ull },
    { 29u, 0.20f, 0, 0x615460e102e713e9ull },
    { 29u, 0.20f, 1, 0xb7ccf0a659b6a966ull },
    { 29u, 0.50f, 0, 0xa34c6887dba539cdull },
    { 29u, 0.50f, 1, 0x9f6f1869843bb5cdull },
    { 29u, 1.00f, 0, 0x4a9a49852eca7c51ull },
    { 29u, 1.00f, 1, 0x3d2ba36a47a52dfaull },
    { 101u, 0.00f, 0, 0x9b3752436e2abbc7ull },
    { 101u, 0.00f, 1, 0x25c1686c20adf160ull },
    { 101u, 0.15f, 0, 0xd426d6072c9ec137ull },
    { 101u, 0.15f, 1, 0x596dd18cb7437e59ull },
    { 101u, 0.20f, 0, 0x12b3eb61157f6424ull },
    { 101u, 0.20f, 1, 0xf2c13e89200f5fc3ull },
    { 101u, 0.50f, 0, 0x21c1daac1275dc35ull },
    { 101u, 0.50f, 1, 0xf867e694ef86fb91ull },
    { 101u, 1.00f, 0, 0x43108ba1e61d047bull },
    { 101u, 1.00f, 1, 0x86b764d0c084db3cull },
    { 4242u, 0.00f, 0, 0x4867943bfaaf31faull },
    { 4242u, 0.00f, 1, 0x885580e9e12bebd0ull },
    { 4242u, 0.15f, 0, 0x899e4f056b5b12caull },
    { 4242u, 0.15f, 1, 0x535cb608119df541ull },
    { 4242u, 0.20f, 0, 0xbfdf0545c38b2d96ull },
    { 4242u, 0.20f, 1, 0x15ce6a7528544643ull },
    { 4242u, 0.50f, 0, 0xf985eb29dd7eec5bull },
    { 4242u, 0.50f, 1, 0xc891590126e01dd0ull },
    { 4242u, 1.00f, 0, 0xa005d96ba19f2847ull },
    { 4242u, 1.00f, 1, 0x3c0b3724434cb60aull },
    { 99999u, 0.00f, 0, 0xdce78f0732d4a5e1ull },
    { 99999u, 0.00f, 1, 0x6ff5a590b94f6290ull },
    { 99999u, 0.15f, 0, 0xde61483264a5b98eull },
    { 99999u, 0.15f, 1, 0x6c0a8f6fc872f34aull },
    { 99999u, 0.20f, 0, 0x70edbbaf290c020aull },
    { 99999u, 0.20f, 1, 0x5eff05cacd959ec6ull },
    { 99999u, 0.50f, 0, 0xbe122644f0302291ull },
    { 99999u, 0.50f, 1, 0xfa6d4d8cb6a583d2ull },
    { 99999u, 1.00f, 0, 0xa3fce559b824ec9cull },
    { 99999u, 1.00f, 1, 0x66d9f813cae85424ull },
    { 999999u, 0.00f, 0, 0x36c020ea69d09cbdull },
    { 999999u, 0.00f, 1, 0x17993f825e98ab53ull },
    { 999999u, 0.15f, 0, 0x079c549153614ef2ull },
    { 999999u, 0.15f, 1, 0xa54440ad34054c3eull },
    { 999999u, 0.20f, 0, 0x3429413b39c5d99dull },
    { 999999u, 0.20f, 1, 0x23d744fedb0a525eull },
    { 999999u, 0.50f, 0, 0x7b97595f5f17b0a0ull },
    { 999999u, 0.50f, 1, 0x6e82fcb63bd46f87ull },
    { 999999u, 1.00f, 0, 0xf4b4bee587c0d2b8ull },
    { 999999u, 1.00f, 1, 0x5c783dd5453b32b2ull }
};

int main()
{
    int bad = 0;
    for (auto& g : kGolden)
    {
        float v[kNumParams];
        randomizeValues (g.seed, g.chaos, v);
        if (g.tune) correctTune (v);
        unsigned long long h = 1469598103934665603ull;
        for (int i = 0; i < 48; ++i)                       // the parameters that existed in the first release
        {
            uint32_t b; std::memcpy (&b, &v[i], 4);
            for (int k = 0; k < 4; ++k) { h ^= (b >> (8 * k)) & 0xff; h *= 1099511628211ull; }
        }
        if (h != g.hash) { ++bad; std::printf ("MISMATCH seed %u chaos %.2f tune %d\n", g.seed, g.chaos, g.tune); }
    }
    static_assert (kNumParams >= 48, "parameters were removed");
    static_assert (kParams[47].id[0] == 'a' && kParams[47].id[4] == 'c', "first-release parameter order changed (adj_clip must stay at index 47)");
    std::printf (bad == 0 ? "SEED COMPATIBILITY OK (%d patches)\n" : "%d MISMATCHES\n", bad == 0 ? (int) (sizeof (kGolden) / sizeof (kGolden[0])) : bad);
    return bad == 0 ? 0 : 1;
}
