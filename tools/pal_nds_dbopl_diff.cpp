/* Bit-exact differential check for the Nintendo DS DBOPL2 fast path. */

#include "../adplug/dosbox/dosbox.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace ReferenceDbOpl
{
#define DB_FASTCALL
#define DBOPL_WAVE 12
#undef SDLPAL_DBOPL_H
#include "../adplug/dosbox/dbopl.h"
#include "../adplug/dosbox/dbopl.cpp.h"
#undef DBOPL_WAVE
#undef DB_FASTCALL
}

namespace FastDbOpl
{
#define DB_FASTCALL
#define DBOPL_WAVE 12
#define PAL_DBOPL_OPL2_ONLY 1
#define PAL_DBOPL_DISABLE_PERCUSSION 1
#define PAL_DBOPL_SIMPLE_VOLUME_HANDLER 1
#define PAL_DBOPL_PRECALCULATE_ENVELOPES 1
#define PAL_DBOPL_ENVELOPE_BUFFER_SAMPLES 469
#undef SDLPAL_DBOPL_H
#include "../adplug/dosbox/dbopl.h"
#include "../adplug/dosbox/dbopl.cpp.h"
#undef PAL_DBOPL_OPL2_ONLY
#undef PAL_DBOPL_DISABLE_PERCUSSION
#undef PAL_DBOPL_SIMPLE_VOLUME_HANDLER
#undef PAL_DBOPL_ENVELOPE_BUFFER_SAMPLES
#undef PAL_DBOPL_PRECALCULATE_ENVELOPES
#undef DBOPL_WAVE
#undef DB_FASTCALL
}

static uint32_t
next_random(uint32_t &state)
{
   state ^= state << 13;
   state ^= state >> 17;
   state ^= state << 5;
   return state;
}

int
main()
{
   ReferenceDbOpl::Chip reference;
   FastDbOpl::Chip fast;
   int32_t expected[469];
   int32_t actual[469];
   uint32_t random = 0x4f504c32u;
   uint64_t compared_samples = 0;

   ReferenceDbOpl::InitTables();
   FastDbOpl::InitTables();
   reference.Setup(32768);
   fast.Setup(32768);

   for (unsigned block = 0; block < 20000; ++block)
   {
      const unsigned writes = next_random(random) & 15u;
      for (unsigned i = 0; i < writes; ++i)
      {
         const uint8_t reg = static_cast<uint8_t>(next_random(random));
         uint8_t value = static_cast<uint8_t>(next_random(random));

         if (reg == 0xbdu)
         {
            value &= 0xc0u;
         }
         reference.WriteReg(reg, value);
         fast.WriteReg(reg, value);
      }
      const unsigned frames = 1u + next_random(random) % 469u;
      reference.GenerateBlock2(frames, expected);
      fast.GenerateBlock2(frames, actual);
      for (unsigned channel = 0; channel < 9; ++channel)
      {
         for (unsigned op = 0; op < 2; ++op)
         {
            const ReferenceDbOpl::Operator &a = reference.chan[channel].op[op];
            const FastDbOpl::Operator &b = fast.chan[channel].op[op];
            if (a.state != b.state || a.volume != b.volume ||
                a.rateIndex != b.rateIndex)
            {
               std::fprintf(stderr,
                  "state mismatch block=%u frames=%u ch=%u op=%u "
                  "state=%u/%u volume=%ld/%ld rate=%lu/%lu "
                  "wave=%lu/%lu reg20=%02x/%02x\n",
                  block, frames, channel, op, a.state, b.state,
                  static_cast<long>(a.volume), static_cast<long>(b.volume),
                  static_cast<unsigned long>(a.rateIndex),
                  static_cast<unsigned long>(b.rateIndex),
                  static_cast<unsigned long>(a.waveIndex),
                  static_cast<unsigned long>(b.waveIndex), a.reg20, b.reg20);
               return EXIT_FAILURE;
            }
         }
      }
      for (unsigned i = 0; i < frames; ++i)
      {
         if (expected[i] != actual[i])
         {
            std::fprintf(stderr,
               "mismatch block=%u sample=%u expected=%ld actual=%ld\n",
               block, i, static_cast<long>(expected[i]),
               static_cast<long>(actual[i]));
            for (unsigned channel = 0; channel < 9; ++channel)
            {
               for (unsigned op = 0; op < 2; ++op)
               {
                  const ReferenceDbOpl::Operator &a =
                     reference.chan[channel].op[op];
                  const FastDbOpl::Operator &b = fast.chan[channel].op[op];
                  if (a.state != b.state || a.volume != b.volume ||
                      a.rateIndex != b.rateIndex || a.waveIndex != b.waveIndex)
                  {
                     std::fprintf(stderr,
                        "  ch=%u op=%u state=%u/%u volume=%ld/%ld "
                        "rate=%lu/%lu wave=%lu/%lu reg20=%02x/%02x\n",
                        channel, op, a.state, b.state,
                        static_cast<long>(a.volume),
                        static_cast<long>(b.volume),
                        static_cast<unsigned long>(a.rateIndex),
                        static_cast<unsigned long>(b.rateIndex),
                        static_cast<unsigned long>(a.waveIndex),
                        static_cast<unsigned long>(b.waveIndex),
                        a.reg20, b.reg20);
                  }
               }
            }
            return EXIT_FAILURE;
         }
      }
      compared_samples += frames;
   }
   std::printf("DBOPL2 melodic differential check: %llu samples bit-exact\n",
      static_cast<unsigned long long>(compared_samples));
   return EXIT_SUCCESS;
}
