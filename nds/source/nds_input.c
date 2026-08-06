#include "SDL.h"

#include <nds.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef struct PalNdsLogicalKey {
   uint32_t physical_mask;
   int sdl_key;
   bool held;
} PalNdsLogicalKey;

static PalNdsLogicalKey pal_nds_keys[] = {
   { KEY_UP, SDLK_UP, false },
   { KEY_DOWN, SDLK_DOWN, false },
   { KEY_LEFT, SDLK_LEFT, false },
   { KEY_RIGHT, SDLK_RIGHT, false },
   { KEY_A | KEY_START, SDLK_RETURN, false },
   { KEY_B | KEY_SELECT, SDLK_ESCAPE, false },
   { KEY_L, SDLK_PAGEUP, false },
   { KEY_R, SDLK_PAGEDOWN, false },
   { KEY_X, SDLK_s, false },
   { KEY_Y, SDLK_d, false },
};

static SDL_Event pal_nds_events[
   sizeof(pal_nds_keys) / sizeof(pal_nds_keys[0])];
static unsigned pal_nds_event_count;
static unsigned pal_nds_event_cursor;

static void
pal_nds_collect_events(
   void)
{
   uint32_t physical;
   unsigned i;

   scanKeys();
   physical = (uint32_t)keysHeld();
   pal_nds_event_count = 0u;
   pal_nds_event_cursor = 0u;

   for (i = 0u; i < sizeof(pal_nds_keys) / sizeof(pal_nds_keys[0]); i++)
   {
      PalNdsLogicalKey *key = &pal_nds_keys[i];
      bool held = (physical & key->physical_mask) != 0u;
      SDL_Event *event;

      if (held == key->held)
      {
         continue;
      }
      event = &pal_nds_events[pal_nds_event_count++];
      memset(event, 0, sizeof(*event));
      event->type = held ? SDL_KEYDOWN : SDL_KEYUP;
      event->key.type = event->type;
      event->key.repeat = 0;
      event->key.keysym.sym = key->sdl_key;
      event->key.keysym.mod = 0;
      key->held = held;
   }
}

int
PalEngineBridge_PollEvent(
   SDL_Event *event)
{
   if (event == NULL)
   {
      return 0;
   }
   if (pal_nds_event_cursor == pal_nds_event_count)
   {
      pal_nds_collect_events();
   }
   if (pal_nds_event_cursor == pal_nds_event_count)
   {
      return 0;
   }
   *event = pal_nds_events[pal_nds_event_cursor++];
   return 1;
}
