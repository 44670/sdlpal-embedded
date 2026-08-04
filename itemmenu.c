/* -*- mode: c; tab-width: 4; c-basic-offset: 4; c-file-style: "linux" -*- */
//
// Copyright (c) 2009-2011, Wei Mingzhi <whistler_wmz@users.sf.net>.
// Copyright (c) 2011-2026, SDLPAL development team.
// All rights reserved.
//
// This file is part of SDLPAL.
//
// SDLPAL is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License, version 3
// as published by the Free Software Foundation.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#include "main.h"
#if defined(PAL_EXTREME_TWO_SCREENS)
#include "embedded/pal_native_ui.h"
#endif

static int     g_iNumInventory = 0;
static WORD    g_wItemFlags = 0;
static BOOL    g_fNoDesc = FALSE;

#if defined(PAL_EXTREME_TWO_SCREENS)
static int     g_iItemMenuColumns = 1;
static int     g_iItemMenuColumnWidth = 1;
static int     g_iItemMenuRows = 1;
static int     g_iItemMenuPreviewSize = 1;
static int     g_iItemMenuPreviewTop = 0;

#define PAL_ITEMMENU_MARGIN_X       6
#define PAL_ITEMMENU_LIST_TOP       2
#define PAL_ITEMMENU_ROW_HEIGHT     12

static BOOL
PAL_ItemMenuBlitRleFit(
   LPCBITMAPRLE      rle,
   UINT              size,
   PalNativeUiRect   box
)
{
   return rle != NULL && size > 0 &&
      PalNativeUi_BlitRleFitIndexed(
         rle, size, (LPBYTE)gpScreen->pixels,
         (uint16_t)gpScreen->pitch, (uint16_t)gpScreen->w,
         (uint16_t)gpScreen->h, box, NULL);
}

static BOOL
PAL_ItemMenuBlitUiFrameFit(
   INT               frame,
   PalNativeUiRect   box
)
{
   LPCBITMAPRLE rle = PAL_SpriteGetFrame(gpSpriteUI, frame);
   LPCBITMAPRLE next = PAL_SpriteGetFrame(gpSpriteUI, frame + 1);
   INT spriteSize = PAL_MKFGetChunkSize(
      CHUNKNUM_SPRITEUI, gpGlobals->f.fpDATA);
   LPCBYTE spriteEnd = spriteSize > 0 ?
      (LPCBYTE)gpSpriteUI + spriteSize : NULL;
   UINT rleSize;

   if (rle == NULL || spriteEnd == NULL || (LPCBYTE)rle >= spriteEnd)
   {
      return FALSE;
   }
   rleSize = next != NULL && next > rle && (LPCBYTE)next <= spriteEnd ?
      (UINT)(next - rle) : (UINT)(spriteEnd - (LPCBYTE)rle);
   return PAL_ItemMenuBlitRleFit(rle, rleSize, box);
}

static PAL_POS
PAL_ItemMenuCellPosition(
   INT               column,
   INT               row
)
{
   return PAL_XY(
      PAL_ITEMMENU_MARGIN_X + column * g_iItemMenuColumnWidth,
      PAL_ITEMMENU_LIST_TOP + row * PAL_ITEMMENU_ROW_HEIGHT);
}

static VOID
PAL_ItemMenuConfigureNativeLayout(
   VOID
)
{
   const INT availableWidth = max(1,
      gpScreen->w - PAL_ITEMMENU_MARGIN_X * 2);
   const INT quantityWidth = PAL_TextWidth(L"99");
   const INT originalColumns = max(1,
      32 / max(1, (INT)gConfig.dwWordLength));
   INT maxNameWidth = 0;
   INT requiredWidth;
   INT previewBottom;
   INT previewTarget;
   INT previewAvailable;
   INT i;

   for (i = 0; i < g_iNumInventory; i++)
   {
      maxNameWidth = max(maxNameWidth,
         PAL_TextWidth(PAL_GetWord(gpGlobals->rgInventory[i].wItem)));
   }
   requiredWidth = max(1, maxNameWidth + quantityWidth + 8);
   g_iItemMenuColumns = min(originalColumns,
      max(1, availableWidth / requiredWidth));
   g_iItemMenuColumnWidth = max(1,
      availableWidth / g_iItemMenuColumns);

   g_iItemMenuRows = gpGlobals->fInBattle ? 5 : 7;
   previewBottom = gpGlobals->fInBattle ?
      gpScreen->h - (gpScreen->w < 200 ?
         PAL_FontHeight() * 2 + 4 : 35) : gpScreen->h;
   previewBottom = max(1, previewBottom);
   previewTarget = max(1, min(64, gpScreen->w / 5));
   previewAvailable = previewBottom -
      (PAL_ITEMMENU_LIST_TOP +
         g_iItemMenuRows * PAL_ITEMMENU_ROW_HEIGHT) - 2;
   while (g_iItemMenuRows > 1 && previewAvailable < 16)
   {
      g_iItemMenuRows--;
      previewAvailable += PAL_ITEMMENU_ROW_HEIGHT;
   }
   g_iItemMenuPreviewSize = max(1,
      min(previewTarget, previewAvailable));
   g_iItemMenuPreviewTop = max(0,
      previewBottom - g_iItemMenuPreviewSize);
}

VOID
PAL_ItemSelectMenuGetPreviewRect(
   SDL_Rect        *rect
)
{
   if (rect != NULL)
   {
      rect->x = 2;
      rect->y = g_iItemMenuPreviewTop;
      rect->w = g_iItemMenuPreviewSize;
      rect->h = g_iItemMenuPreviewSize;
   }
}
#endif

WORD
PAL_ItemSelectMenuUpdate(
   VOID
)
/*++
  Purpose:

    Initialize the item selection menu.

  Parameters:

    None.

  Return value:

    The object ID of the selected item. 0 if cancelled, 0xFFFF if not confirmed.

--*/
{
   int                i, j, k, line, item_delta, item_x, item_y;
   WORD               wObject, wScript;
   BYTE               bColor;
#if !defined(PAL_EXTREME_TWO_SCREENS)
   static BYTE        bufImage[2048];
#endif
#if defined(PAL_EXTREME_TWO_SCREENS)
   const int          iItemsPerLine = g_iItemMenuColumns;
   const int          iItemTextWidth = g_iItemMenuColumnWidth;
   const int          iInfoTop = g_iItemMenuPreviewTop;
   const int          iLinesPerPage = g_iItemMenuRows;
   const int          iCursorXOffset = 5;
#else
   const int          iItemsPerLine = 32 / gConfig.dwWordLength;
   const int          iItemTextWidth = 8 * gConfig.dwWordLength + 20;
   const int          iLinesPerPage = 7 - gConfig.ScreenLayout.ExtraItemDescLines;
   const int          iCursorXOffset = gConfig.dwWordLength * 5 / 2;
   const int          iAmountXOffset = gConfig.dwWordLength * 8 + 1;
   const int          iPictureYOffset = (gConfig.ScreenLayout.ExtraItemDescLines > 1) ? (gConfig.ScreenLayout.ExtraItemDescLines - 1) * 16 : 0;
#endif
   const int          iPageLineOffset = (iLinesPerPage + 1) / 2;
#if defined(PAL_EXTREME_TWO_SCREENS)
   PAL_POS            cursorPos = PAL_XY(
      PAL_ITEMMENU_MARGIN_X + iCursorXOffset,
      PAL_ITEMMENU_LIST_TOP + 6);
#else
   PAL_POS            cursorPos = PAL_XY(15 + iCursorXOffset, 22);
#endif

   //
   // Process input
   //
   if (g_InputState.dwKeyPress & kKeyUp)
   {
      item_delta = -iItemsPerLine;
   }
   else if (g_InputState.dwKeyPress & kKeyDown)
   {
      item_delta = iItemsPerLine;
   }
   else if (g_InputState.dwKeyPress & kKeyLeft)
   {
      item_delta = -1;
   }
   else if (g_InputState.dwKeyPress & kKeyRight)
   {
      item_delta = 1;
   }
   else if (g_InputState.dwKeyPress & kKeyPgUp)
   {
      item_delta = -(iItemsPerLine * iLinesPerPage);
   }
   else if (g_InputState.dwKeyPress & kKeyPgDn)
   {
      item_delta = iItemsPerLine * iLinesPerPage;
   }
   else if (g_InputState.dwKeyPress & kKeyHome)
   {
      item_delta = -gpGlobals->iCurInvMenuItem;
   }
   else if (g_InputState.dwKeyPress & kKeyEnd)
   {
      item_delta = g_iNumInventory - gpGlobals->iCurInvMenuItem - 1;
   }
   else if (g_InputState.dwKeyPress & kKeyMenu)
   {
      return 0;
   }
   else
   {
      item_delta = 0;
   }

   //
   // Make sure the current menu item index is in bound
   //
   if (gpGlobals->iCurInvMenuItem + item_delta < 0)
      gpGlobals->iCurInvMenuItem = 0;
   else if (gpGlobals->iCurInvMenuItem + item_delta >= g_iNumInventory)
      gpGlobals->iCurInvMenuItem = g_iNumInventory-1;
   else
      gpGlobals->iCurInvMenuItem += item_delta;

   //
   // Redraw the box
   //
#if defined(PAL_EXTREME_TWO_SCREENS)
   {
      LPCBITMAPRLE topLeft = PAL_SpriteGetFrame(gpSpriteUI, 9);
      LPCBITMAPRLE topMiddle = PAL_SpriteGetFrame(gpSpriteUI, 10);
      LPCBITMAPRLE topRight = PAL_SpriteGetFrame(gpSpriteUI, 11);
      LPCBITMAPRLE middleLeft = PAL_SpriteGetFrame(gpSpriteUI, 12);
      LPCBITMAPRLE bottomLeft = PAL_SpriteGetFrame(gpSpriteUI, 15);
      INT listBottom = PAL_ITEMMENU_LIST_TOP +
         iLinesPerPage * PAL_ITEMMENU_ROW_HEIGHT;
      INT middleHeight = max(1, PAL_RLEGetHeight(middleLeft));
      INT boxRows = max(0,
         (listBottom - PAL_RLEGetHeight(topLeft) -
            PAL_RLEGetHeight(bottomLeft) + middleHeight - 1) /
         middleHeight);
      INT middleWidth = max(1, PAL_RLEGetWidth(topMiddle));
      INT boxColumns = max(0,
         (gpScreen->w - 2 - PAL_RLEGetWidth(topLeft) -
            PAL_RLEGetWidth(topRight)) / middleWidth);

      PAL_CreateBoxWithShadow(PAL_XY(2, 0), boxRows, boxColumns, 1,
         FALSE, 0);
   }
#else
   PAL_CreateBoxWithShadow(PAL_XY(2, 0), iLinesPerPage - 1, 17, 1,
      FALSE, 0);
#endif

   //
   // Draw the texts in the current page
   //
   i = gpGlobals->iCurInvMenuItem / iItemsPerLine * iItemsPerLine - iItemsPerLine * iPageLineOffset;
   if (i < 0)
   {
      i = 0;
   }

#if defined(PAL_EXTREME_TWO_SCREENS)
   const int xBase = 2, yBase = iInfoTop;
#else
   const int xBase = 0, yBase = 140;
#endif

   for (j = 0; j < iLinesPerPage; j++)
   {
      for (k = 0; k < iItemsPerLine; k++)
      {
         wObject = gpGlobals->rgInventory[i].wItem;
         bColor = MENUITEM_COLOR;

         if (i >= MAX_INVENTORY || wObject == 0)
         {
            //
            // End of the list reached
            //
            j = iLinesPerPage;
            break;
         }

         if (i == gpGlobals->iCurInvMenuItem)
         {
            if (!(gpGlobals->g.rgObject[wObject].item.wFlags & g_wItemFlags) ||
               (SHORT)gpGlobals->rgInventory[i].nAmount <= (SHORT)gpGlobals->rgInventory[i].nAmountInUse)
            {
               //
               // This item is not selectable
               //
               bColor = MENUITEM_COLOR_SELECTED_INACTIVE;
            }
            else
            {
               //
               // This item is selectable
               //
               if (gpGlobals->rgInventory[i].nAmount == 0)
               {
                  bColor = MENUITEM_COLOR_EQUIPPEDITEM;
               }
               else
               {
                  bColor = MENUITEM_COLOR_SELECTED;
               }
            }
         }
         else if (!(gpGlobals->g.rgObject[wObject].item.wFlags & g_wItemFlags) ||
            (SHORT)gpGlobals->rgInventory[i].nAmount <= (SHORT)gpGlobals->rgInventory[i].nAmountInUse)
         {
            //
            // This item is not selectable
            //
            bColor = MENUITEM_COLOR_INACTIVE;
         }
         else if (gpGlobals->rgInventory[i].nAmount == 0)
         {
            bColor = MENUITEM_COLOR_EQUIPPEDITEM;
         }

#if defined(PAL_EXTREME_TWO_SCREENS)
         {
            PAL_POS itemPos = PAL_ItemMenuCellPosition(k, j);
            item_x = PAL_X(itemPos);
            item_y = PAL_Y(itemPos);
         }
#else
         item_x = 15 + k * iItemTextWidth;
         item_y = 12 + j * 18;
#endif

         //
         // Draw the text
         //
         PAL_DrawText(PAL_GetWord(wObject), PAL_XY(item_x, item_y),
            bColor, TRUE, FALSE, FALSE);

         if (i == gpGlobals->iCurInvMenuItem)
         {
            cursorPos = PAL_XY(item_x + iCursorXOffset,
#if defined(PAL_EXTREME_TWO_SCREENS)
               item_y + 6);
#else
               item_y + 10);
#endif

            //
            // Draw the picture of current selected item
            //
#if !defined(PAL_EXTREME_TWO_SCREENS)
            PAL_RLEBlitToSurfaceWithShadow(PAL_SpriteGetFrame(gpSpriteUI, SPRITENUM_ITEMBOX), gpScreen,
               PAL_XY(xBase + 5, yBase + 5 - iPictureYOffset), TRUE);
#endif
#if defined(PAL_EXTREME_TWO_SCREENS)
            {
               LPCBYTE lpImage;
               UINT uiImageSize;
               INT inset = max(2, g_iItemMenuPreviewSize / 8);
               PalNativeUiRect preview = {
                  (int16_t)xBase,
                  (int16_t)yBase,
                  (uint16_t)g_iItemMenuPreviewSize,
                  (uint16_t)g_iItemMenuPreviewSize
               };
               PalNativeUiRect imageBox = {
                  (int16_t)(xBase + inset),
                  (int16_t)(yBase + inset),
                  (uint16_t)max(1, g_iItemMenuPreviewSize - inset * 2),
                  (uint16_t)max(1, g_iItemMenuPreviewSize - inset * 2)
               };

               (void)PAL_ItemMenuBlitUiFrameFit(
                  SPRITENUM_ITEMBOX, preview);
               if (PAL_MKFMapChunk(gpGlobals->f.fpBALL,
                  gpGlobals->g.rgObject[wObject].item.wBitmap, &lpImage, &uiImageSize) &&
                  uiImageSize > 0)
               {
                  (void)PAL_ItemMenuBlitRleFit(
                     lpImage, uiImageSize, imageBox);
               }
            }
#else
            PAL_RLEBlitToSurface(
               PAL_SpriteGetFrame(gpSpriteUI, SPRITENUM_ITEMBOX), gpScreen,
               PAL_XY(xBase, yBase - iPictureYOffset));
            if (PAL_MKFReadChunk(bufImage, 2048,
               gpGlobals->g.rgObject[wObject].item.wBitmap, gpGlobals->f.fpBALL) > 0)
            {
               PAL_RLEBlitToSurface(bufImage, gpScreen, PAL_XY(xBase + 8, yBase + 7 - iPictureYOffset));
            }
#endif
         }

         //
         // Draw the amount of this item
         //
         if ((SHORT)gpGlobals->rgInventory[i].nAmount - (SHORT)gpGlobals->rgInventory[i].nAmountInUse > 1)
         {
#if defined(PAL_EXTREME_TWO_SCREENS)
            INT amountWidth = PAL_TextWidth(L"99");
            INT cellRight = PAL_ITEMMENU_MARGIN_X +
               (k + 1) * iItemTextWidth - 2;
            PAL_DrawNumber(
               gpGlobals->rgInventory[i].nAmount -
                  gpGlobals->rgInventory[i].nAmountInUse,
               2, PAL_XY(cellRight - amountWidth, item_y),
               kNumColorCyan, kNumAlignRight);
#else
            PAL_DrawNumber(gpGlobals->rgInventory[i].nAmount - gpGlobals->rgInventory[i].nAmountInUse,
               2, PAL_XY(item_x + iAmountXOffset, item_y + 5),
               kNumColorCyan, kNumAlignRight);
#endif
         }

         i++;
      }
   }

   //
   // Draw the cursor on the current selected item
   //
   PAL_RLEBlitToSurface(PAL_SpriteGetFrame(gpSpriteUI, SPRITENUM_CURSOR), gpScreen, cursorPos);
   wObject = gpGlobals->rgInventory[gpGlobals->iCurInvMenuItem].wItem;

   //
   // Draw the description of the selected item
   //
   if (!gConfig.fIsWIN95)
   {
      if (!g_fNoDesc && gpGlobals->lpObjectDesc != NULL)
	  {
         WCHAR szDesc[512], *next;
         const WCHAR *d = PAL_GetObjectDesc(gpGlobals->lpObjectDesc, wObject);

         if (d != NULL)
         {
#if defined(PAL_EXTREME_TWO_SCREENS)
            k = iInfoTop + 2;
#else
            k = 150 - gConfig.ScreenLayout.ExtraItemDescLines * 16;
#endif
            wcscpy(szDesc, d);
            d = szDesc;

            while (TRUE)
            {
               next = wcschr(d, '*');
               if (next != NULL)
               {
                  *next++ = '\0';
               }

#if defined(PAL_EXTREME_TWO_SCREENS)
               PAL_DrawText(d, PAL_XY(
                  xBase + g_iItemMenuPreviewSize + 2, k), DESCTEXT_COLOR,
                  TRUE, FALSE, FALSE);
               k += PAL_FontHeight() + 1;
#else
               PAL_DrawText(d, PAL_XY(75, k), DESCTEXT_COLOR,
                  TRUE, FALSE, FALSE);
               k += 16;
#endif

               if (next == NULL)
               {
                  break;
               }

               d = next;
            }
         }
      }
   }
   else
   {
      if (!g_fNoDesc)
      {
         wScript = gpGlobals->g.rgObject[wObject].item.wScriptDesc;
         line = 0;
         while (wScript && gpGlobals->g.lprgScriptEntry[wScript].wOperation != 0)
         {
            if (gpGlobals->g.lprgScriptEntry[wScript].wOperation == 0xFFFF)
            {
               int line_incr = (gpGlobals->g.lprgScriptEntry[wScript].rgwOperand[1] != 1) ? 1 : 0;
               wScript = PAL_RunAutoScript(wScript, PAL_ITEM_DESC_BOTTOM | line);
               line += line_incr;
            }
            else
            {
               wScript = PAL_RunAutoScript(wScript, 0);
            }
         }
      }
   }

   if (g_InputState.dwKeyPress & kKeySearch)
   {
      if ((gpGlobals->g.rgObject[wObject].item.wFlags & g_wItemFlags) &&
         (SHORT)gpGlobals->rgInventory[gpGlobals->iCurInvMenuItem].nAmount >
         (SHORT)gpGlobals->rgInventory[gpGlobals->iCurInvMenuItem].nAmountInUse)
      {
         if (gpGlobals->rgInventory[gpGlobals->iCurInvMenuItem].nAmount > 0)
         {
            j = (gpGlobals->iCurInvMenuItem < iItemsPerLine * iPageLineOffset) ? (gpGlobals->iCurInvMenuItem / iItemsPerLine) : iPageLineOffset;
            k = gpGlobals->iCurInvMenuItem % iItemsPerLine;

#if defined(PAL_EXTREME_TWO_SCREENS)
            {
               PAL_POS itemPos = PAL_ItemMenuCellPosition(k, j);
               item_x = PAL_X(itemPos);
               item_y = PAL_Y(itemPos);
            }
#else
            item_x = 15 + k * iItemTextWidth;
            item_y = 12 + j * 18;
#endif
            PAL_DrawText(PAL_GetWord(wObject), PAL_XY(item_x, item_y),
               MENUITEM_COLOR_CONFIRMED, FALSE, FALSE, FALSE);

            //
            // Draw the cursor on the current selected item
            //
            PAL_RLEBlitToSurface(PAL_SpriteGetFrame(gpSpriteUI, SPRITENUM_CURSOR), gpScreen, cursorPos);
         }

         return wObject;
      }
   }

   return 0xFFFF;
}

VOID
PAL_ItemSelectMenuInit(
   WORD                      wItemFlags
)
/*++
  Purpose:

    Initialize the item selection menu.

  Parameters:

    [IN]  wItemFlags - flags for usable item.

  Return value:

    None.

--*/
{
   int           i, j;
   WORD          w;

   g_wItemFlags = wItemFlags;

   //
   // Compress the inventory
   //
   PAL_CompressInventory();

   //
   // Count the total number of items in inventory
   //
   g_iNumInventory = 0;
   while (g_iNumInventory < MAX_INVENTORY &&
      gpGlobals->rgInventory[g_iNumInventory].wItem != 0)
   {
      g_iNumInventory++;
   }

   //
   // Also add usable equipped items to the list
   //
   if ((wItemFlags & kItemFlagUsable) && !gpGlobals->fInBattle)
   {
      for (i = 0; i <= gpGlobals->wMaxPartyMemberIndex; i++)
      {
         w = gpGlobals->rgParty[i].wPlayerRole;

         for (j = 0; j < MAX_PLAYER_EQUIPMENTS; j++)
         {
            if (gpGlobals->g.rgObject[gpGlobals->g.PlayerRoles.rgwEquipment[j][w]].item.wFlags & kItemFlagUsable)
            {
               if (g_iNumInventory < MAX_INVENTORY)
               {
                  gpGlobals->rgInventory[g_iNumInventory].wItem = gpGlobals->g.PlayerRoles.rgwEquipment[j][w];
                  gpGlobals->rgInventory[g_iNumInventory].nAmount = 0;
                  gpGlobals->rgInventory[g_iNumInventory].nAmountInUse = (WORD)-1;

                  g_iNumInventory++;
               }
            }
         }
      }
   }
#if defined(PAL_EXTREME_TWO_SCREENS)
   PAL_ItemMenuConfigureNativeLayout();
#endif
}

WORD
PAL_ItemSelectMenu(
   LPITEMCHANGED_CALLBACK    lpfnMenuItemChanged,
   WORD                      wItemFlags
)
/*++
  Purpose:

    Show the item selection menu.

  Parameters:

    [IN]  lpfnMenuItemChanged - Callback function which is called when user
                                changed the current menu item.

    [IN]  wItemFlags - flags for usable item.

  Return value:

    The object ID of the selected item. 0 if cancelled.

--*/
{
   int              iPrevIndex;
   WORD             w;
   DWORD            dwTime;

   PAL_ItemSelectMenuInit(wItemFlags);
   iPrevIndex = gpGlobals->iCurInvMenuItem;

   PAL_ClearKeyState();

   if (lpfnMenuItemChanged != NULL)
   {
      g_fNoDesc = TRUE;
      (*lpfnMenuItemChanged)(gpGlobals->rgInventory[gpGlobals->iCurInvMenuItem].wItem);
   }

   dwTime = SDL_GetTicks();

   while (TRUE)
   {
      if (lpfnMenuItemChanged == NULL)
      {
         PAL_MakeScene();
      }

      w = PAL_ItemSelectMenuUpdate();
      VIDEO_UpdateScreen(NULL);

      PAL_ClearKeyState();

      PAL_ProcessEvent();
      while (!SDL_TICKS_PASSED(SDL_GetTicks(), dwTime))
      {
         PAL_ProcessEvent();
         if (g_InputState.dwKeyPress != 0)
         {
            break;
         }
         SDL_Delay(5);
      }

      dwTime = SDL_GetTicks() + FRAME_TIME;

      if (w != 0xFFFF)
      {
         g_fNoDesc = FALSE;
         return w;
      }

      if (iPrevIndex != gpGlobals->iCurInvMenuItem)
      {
         if (gpGlobals->iCurInvMenuItem >= 0 && gpGlobals->iCurInvMenuItem < MAX_INVENTORY)
         {
            if (lpfnMenuItemChanged != NULL)
            {
               (*lpfnMenuItemChanged)(gpGlobals->rgInventory[gpGlobals->iCurInvMenuItem].wItem);
            }
         }

         iPrevIndex = gpGlobals->iCurInvMenuItem;
      }
   }

   assert(FALSE);
   return 0; // should not really reach here
}
