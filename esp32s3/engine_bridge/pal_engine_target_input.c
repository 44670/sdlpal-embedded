#include "pal_target_board.h"

#include "SDL.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define PAL_ENGINE_TOUCH_VIEW_Y0 ((int)CORES3SE_PAL_Y_OFFSET)
#define PAL_ENGINE_TOUCH_VIEW_H 200
#define PAL_ENGINE_TOUCH_VIEW_Y1 (PAL_ENGINE_TOUCH_VIEW_Y0 + PAL_ENGINE_TOUCH_VIEW_H)
#define PAL_ENGINE_TOUCH_COL_W ((int)CORES3SE_LCD_WIDTH / 3)
#define PAL_ENGINE_TOUCH_ROW_H (PAL_ENGINE_TOUCH_VIEW_H / 3)

static int pal_engine_current_key;
static int pal_engine_pending_key;

#if !defined(PAL_CARDPUTER_EXTREME)
static int
touch_key_from_point(
   uint16_t x,
   uint16_t y
)
{
   int local_y;
   int col;
   int row;

   if (y < CORES3SE_PAL_Y_OFFSET)
   {
      return SDLK_ESCAPE;
   }
   if (y >= PAL_ENGINE_TOUCH_VIEW_Y1)
   {
      return SDLK_RETURN;
   }

   local_y = (int)y - PAL_ENGINE_TOUCH_VIEW_Y0;
   col = (int)x / PAL_ENGINE_TOUCH_COL_W;
   row = local_y / PAL_ENGINE_TOUCH_ROW_H;
   if (col > 2)
   {
      col = 2;
   }
   if (row > 2)
   {
      row = 2;
   }

   if (col == 1 && row == 1)
   {
      return SDLK_RETURN;
   }
   if (row == 0)
   {
      return SDLK_UP;
   }
   if (row == 2)
   {
      return SDLK_DOWN;
   }
   if (col == 0)
   {
      return SDLK_LEFT;
   }
   return SDLK_RIGHT;
}
#else
static int
cardputer_key_to_sdl(
   uint8_t key
)
{
   switch (key)
   {
   case 'i':
   case 'I':
      return SDLK_UP;
   case 'k':
   case 'K':
      return SDLK_DOWN;
   case 'j':
   case 'J':
      return SDLK_LEFT;
   case 'l':
   case 'L':
      return SDLK_RIGHT;
   case '\r':
   case '\n':
   case ' ':
      return SDLK_RETURN;
   case '\b':
   case 0x7f:
      return SDLK_ESCAPE;
   case '[':
      return SDLK_PAGEUP;
   case ']':
      return SDLK_PAGEDOWN;
   default:
      return (int)key;
   }
}
#endif

static void
make_key_event(
   SDL_Event *event,
   uint32_t type,
   int key
)
{
   memset(event, 0, sizeof(*event));
   event->type = type;
   event->key.type = type;
   event->key.repeat = 0;
   event->key.keysym.sym = key;
   event->key.keysym.mod = 0;
}

int
PalEngineBridge_PollEvent(
   SDL_Event *event
)
{
#if defined(PAL_CARDPUTER_EXTREME)
   uint8_t raw_key = 0;
   bool pressed = false;
#else
   uint16_t x = 0;
   uint16_t y = 0;
   int desired_key = 0;
#endif

   if (event == NULL)
   {
      return 0;
   }

   if (pal_engine_pending_key != 0)
   {
      pal_engine_current_key = pal_engine_pending_key;
      pal_engine_pending_key = 0;
      make_key_event(event, SDL_KEYDOWN, pal_engine_current_key);
      return 1;
   }

#if defined(PAL_CARDPUTER_EXTREME)
   if (!CardputerExtreme_PollKey(&raw_key, &pressed))
   {
      return 0;
   }
   {
      int key = cardputer_key_to_sdl(raw_key);
      if (key == 0)
      {
         return 0;
      }
      make_key_event(event, pressed ? SDL_KEYDOWN : SDL_KEYUP, key);
      return 1;
   }
#else
   if (CoreS3Se_TouchPoint(&x, &y))
   {
      desired_key = touch_key_from_point(x, y);
   }

   if (desired_key == pal_engine_current_key)
   {
      return 0;
   }
   if (pal_engine_current_key != 0)
   {
      int old_key = pal_engine_current_key;
      pal_engine_current_key = 0;
      pal_engine_pending_key = desired_key;
      make_key_event(event, SDL_KEYUP, old_key);
      return 1;
   }
   if (desired_key != 0)
   {
      pal_engine_current_key = desired_key;
      make_key_event(event, SDL_KEYDOWN, desired_key);
      return 1;
   }

   return 0;
#endif
}
