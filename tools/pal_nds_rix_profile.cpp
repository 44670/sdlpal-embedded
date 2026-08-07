/*
 * Profile a native PAL MUS/RIX archive as OPL2 register events.
 *
 * This host-only tool deliberately stops before choosing an NDS playback
 * representation.  Its output is useful to both a sampled-instrument compiler
 * and a whole-track PCM compiler: it reports duration, note/key activity,
 * unique key-on patches, live operator changes, and rhythm-mode activity.
 */

#include "../adplug/rix.h"
#include "../embedded/pal_music_cache.h"
#include "../embedded/pal_pack.h"

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <set>

namespace
{

constexpr uint32_t kRixTicksPerSecond = 70u;
constexpr uint32_t kMaximumTrackTicks = kRixTicksPerSecond * 60u * 10u + 1u;

/* Keep these OPL2 operator slot maps in sync with nds/source/nds_music.cpp. */
constexpr uint8_t kModulatorSlot[9] = {
   0u, 1u, 2u, 8u, 9u, 10u, 16u, 17u, 18u,
};
constexpr uint8_t kCarrierSlot[9] = {
   3u, 4u, 5u, 11u, 12u, 13u, 19u, 20u, 21u,
};

struct Patch
{
   std::array<uint8_t, 11> bytes;

   bool operator<(const Patch &other) const
   {
      return bytes < other.bytes;
   }
};

struct TrackStats
{
   uint64_t note_ons;
   uint64_t pitch_writes;
   uint64_t live_operator_writes;
   uint64_t live_groups[5];
   uint64_t rhythm_writes;
};

class ProfileOpl final : public Copl
{
public:
   ProfileOpl() : Copl(TYPE_OPL2)
   {
      reset_registers();
      reset_stats();
   }

   void reset_stats()
   {
      patches.clear();
      memset(&stats, 0, sizeof(stats));
   }

   void init() override
   {
      reset_registers();
   }

   void write(int reg_value, int value) override
   {
      const unsigned reg = static_cast<unsigned>(reg_value) & 0xffu;
      const uint8_t byte = static_cast<uint8_t>(value);
      const uint8_t old = regs[reg];

      regs[reg] = byte;
      if (reg >= 0xb0u && reg <= 0xb8u)
      {
         const unsigned channel = reg - 0xb0u;
         const bool now_held = (byte & 0x20u) != 0u;

         if (now_held && !held[channel])
         {
            patches.insert(capture_patch(channel));
            stats.note_ons++;
         }
         held[channel] = now_held;
      }
      if (reg == 0xbdu && ((old ^ byte) & 0x3fu) != 0u)
      {
         stats.rhythm_writes++;
      }
      if (reg >= 0xa0u && reg <= 0xb8u && old != byte)
      {
         stats.pitch_writes++;
      }
      if (is_operator_register(reg) && old != byte)
      {
         for (unsigned channel = 0u; channel < 9u; channel++)
         {
            if (held[channel] && register_affects_channel(reg, channel))
            {
               stats.live_operator_writes++;
               stats.live_groups[operator_group(reg)]++;
            }
         }
      }
   }

   void update(short *, int) override {}

   bool getstereo() override
   {
      return false;
   }

   TrackStats stats;
   std::set<Patch> patches;

private:
   uint8_t regs[256];
   bool held[9];

   void reset_registers()
   {
      memset(regs, 0, sizeof(regs));
      memset(held, 0, sizeof(held));
   }

   static bool is_operator_register(unsigned reg)
   {
      return (reg >= 0x20u && reg <= 0x95u) ||
         (reg >= 0xe0u && reg <= 0xf5u);
   }

   static unsigned operator_group(unsigned reg)
   {
      const unsigned high = reg & 0xe0u;

      return high == 0x20u ? 0u :
         high == 0x40u ? 1u :
         high == 0x60u ? 2u :
         high == 0x80u ? 3u : 4u;
   }

   static bool register_affects_channel(unsigned reg, unsigned channel)
   {
      const unsigned slot = reg & 0x1fu;

      return slot == kModulatorSlot[channel] ||
         slot == kCarrierSlot[channel];
   }

   Patch capture_patch(unsigned channel) const
   {
      static constexpr uint8_t bases[5] = {
         0x20u, 0x40u, 0x60u, 0x80u, 0xe0u,
      };
      Patch patch = {};

      for (unsigned group = 0u; group < 5u; group++)
      {
         patch.bytes[group * 2u] =
            regs[bases[group] + kModulatorSlot[channel]];
         patch.bytes[group * 2u + 1u] =
            regs[bases[group] + kCarrierSlot[channel]];
      }
      patch.bytes[10] = regs[0xc0u + channel];
      return patch;
   }
};

bool parse_track_filter(const char *text, int *track_filter)
{
   char *end = nullptr;
   const long value = strtol(text, &end, 10);

   if (end == text || *end != '\0' || value < 0 || value > UINT16_MAX)
   {
      return false;
   }
   *track_filter = static_cast<int>(value);
   return true;
}

} // namespace

int
main(int argc, char **argv)
{
   const char *path;
   int track_filter = -1;
   int fd;
   struct stat status;
   const uint8_t *image;
   PalPack pack;
   uint16_t chunk_count;
   ProfileOpl opl;
   CrixPlayer decoder(&opl);
   std::set<Patch> all_patches;
   TrackStats total_stats = {};
   uint64_t total_ticks = 0u;
   uint64_t maximum_ticks = 0u;
   unsigned maximum_track = 0u;
   unsigned mapped_tracks = 0u;

   if (argc != 2 && argc != 4)
   {
      fprintf(stderr, "usage: %s PAL_FULL.PAK [--track NUMBER]\n", argv[0]);
      return 2;
   }
   path = argv[1];
   if (argc == 4 &&
      (strcmp(argv[2], "--track") != 0 ||
       !parse_track_filter(argv[3], &track_filter)))
   {
      fprintf(stderr, "invalid track filter\n");
      return 2;
   }

   fd = open(path, O_RDONLY);
   if (fd < 0 || fstat(fd, &status) != 0 || status.st_size <= 0 ||
      static_cast<uint64_t>(status.st_size) > UINT32_MAX)
   {
      if (fd >= 0)
      {
         close(fd);
      }
      fprintf(stderr, "cannot open pack: %s\n", path);
      return 3;
   }
   image = static_cast<const uint8_t *>(mmap(
      nullptr, static_cast<size_t>(status.st_size), PROT_READ, MAP_PRIVATE,
      fd, 0));
   close(fd);
   if (image == MAP_FAILED ||
      !PalPack_OpenConst(&pack, image, static_cast<uint32_t>(status.st_size)) ||
      !PalPack_GetChunkCount(&pack, PAL_PACK_ARCHIVE_MUS, &chunk_count))
   {
      fprintf(stderr, "invalid pack or missing MUS archive\n");
      return 4;
   }

   printf("track\tbytes\tticks\tseconds\tpatches\tnote_on\tpitch\t"
      "live_op\tlive_20\tlive_40\tlive_60\tlive_80\tlive_e0\trhythm\n");
   for (uint16_t track_number = 0u;
      track_number < chunk_count;
      track_number++)
   {
      PalMusicTrack track = {};
      uint64_t ticks = 0u;

      if (track_filter >= 0 && track_number != track_filter)
      {
         continue;
      }
      if (!PalMusic_MapMus(&pack, track_number, &track))
      {
         continue;
      }
      opl.reset_stats();
      if (!decoder.load_buffer(track.data, track.size))
      {
         fprintf(stderr, "invalid RIX track: %u\n", track_number);
         return 5;
      }
      while (ticks < kMaximumTrackTicks && decoder.update())
      {
         ticks++;
      }
      if (ticks == kMaximumTrackTicks)
      {
         fprintf(stderr, "RIX track exceeds ten minutes: %u\n", track_number);
         return 6;
      }

      printf("%u\t%u\t%llu\t%.3f\t%zu\t%llu\t%llu\t%llu\t"
         "%llu\t%llu\t%llu\t%llu\t%llu\t%llu\n",
         track_number,
         track.size,
         static_cast<unsigned long long>(ticks),
         static_cast<double>(ticks) / kRixTicksPerSecond,
         opl.patches.size(),
         static_cast<unsigned long long>(opl.stats.note_ons),
         static_cast<unsigned long long>(opl.stats.pitch_writes),
         static_cast<unsigned long long>(opl.stats.live_operator_writes),
         static_cast<unsigned long long>(opl.stats.live_groups[0]),
         static_cast<unsigned long long>(opl.stats.live_groups[1]),
         static_cast<unsigned long long>(opl.stats.live_groups[2]),
         static_cast<unsigned long long>(opl.stats.live_groups[3]),
         static_cast<unsigned long long>(opl.stats.live_groups[4]),
         static_cast<unsigned long long>(opl.stats.rhythm_writes));

      all_patches.insert(opl.patches.begin(), opl.patches.end());
      total_ticks += ticks;
      total_stats.note_ons += opl.stats.note_ons;
      total_stats.pitch_writes += opl.stats.pitch_writes;
      total_stats.live_operator_writes += opl.stats.live_operator_writes;
      total_stats.rhythm_writes += opl.stats.rhythm_writes;
      for (unsigned group = 0u; group < 5u; group++)
      {
         total_stats.live_groups[group] += opl.stats.live_groups[group];
      }
      mapped_tracks++;
      if (ticks > maximum_ticks)
      {
         maximum_ticks = ticks;
         maximum_track = track_number;
      }
   }

   fprintf(stderr,
      "tracks=%u ticks=%llu seconds=%.3f unique_patches=%zu "
      "note_on=%llu pitch=%llu live_op=%llu "
      "live_20=%llu live_40=%llu live_60=%llu live_80=%llu live_e0=%llu "
      "rhythm=%llu max_track=%u max_seconds=%.3f\n",
      mapped_tracks,
      static_cast<unsigned long long>(total_ticks),
      static_cast<double>(total_ticks) / kRixTicksPerSecond,
      all_patches.size(),
      static_cast<unsigned long long>(total_stats.note_ons),
      static_cast<unsigned long long>(total_stats.pitch_writes),
      static_cast<unsigned long long>(total_stats.live_operator_writes),
      static_cast<unsigned long long>(total_stats.live_groups[0]),
      static_cast<unsigned long long>(total_stats.live_groups[1]),
      static_cast<unsigned long long>(total_stats.live_groups[2]),
      static_cast<unsigned long long>(total_stats.live_groups[3]),
      static_cast<unsigned long long>(total_stats.live_groups[4]),
      static_cast<unsigned long long>(total_stats.rhythm_writes),
      maximum_track,
      static_cast<double>(maximum_ticks) / kRixTicksPerSecond);

   munmap(const_cast<uint8_t *>(image), static_cast<size_t>(status.st_size));
   return mapped_tracks != 0u ? 0 : 7;
}
