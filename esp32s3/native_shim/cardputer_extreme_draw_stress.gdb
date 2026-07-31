set pagination off
set confirm off
set breakpoint pending off

break PAL_StartFrame if gpGlobals && gpGlobals->wNumScene == 1 && gpGlobals->dwFrameNum == 0
run

set gpGlobals->wNumScene=20
set gpGlobals->wLayer=0
set gpGlobals->wPartyDirection=3
set gpGlobals->g.PlayerRoles.rgwSpriteNum[0]=193
set gpGlobals->g.PlayerRoles.rgwSpriteNum[1]=361
set gpGlobals->g.PlayerRoles.rgwSpriteNum[2]=361
set gpGlobals->wMaxPartyMemberIndex=2
set gpGlobals->nFollower=2
set gpGlobals->rgParty[0].wPlayerRole=0
set gpGlobals->rgParty[1].wPlayerRole=1
set gpGlobals->rgParty[2].wPlayerRole=2
set gpGlobals->rgParty[3].wPlayerRole=5
set gpGlobals->rgParty[4].wPlayerRole=2
call PAL_SetLoadFlags(6)
call PAL_LoadResources()

set gpGlobals->viewport=0x040b0200
set gpGlobals->partyoffset=0x007000a0
set gpGlobals->rgParty[0].x=160
set gpGlobals->rgParty[0].y=112
set gpGlobals->rgParty[1].x=144
set gpGlobals->rgParty[1].y=104
set gpGlobals->rgParty[2].x=128
set gpGlobals->rgParty[2].y=96
set gpGlobals->rgParty[3].x=112
set gpGlobals->rgParty[3].y=88
set gpGlobals->rgParty[4].x=96
set gpGlobals->rgParty[4].y=80
set gpGlobals->rgParty[0].wFrame=6
set gpGlobals->rgParty[1].wFrame=4
set gpGlobals->rgParty[2].wFrame=4
set gpGlobals->rgParty[3].wFrame=2
set gpGlobals->rgParty[4].wFrame=11

set $event_object=(EVENTOBJECT *)&pal_sram_extreme_event_sector
call PAL_EventObjectRead(357, $event_object)
set $event_object->sState=1
set $event_object->sVanishTime=0
call PAL_EventObjectWrite(357, $event_object)
call PAL_EventObjectRead(358, $event_object)
set $event_object->sState=1
set $event_object->sVanishTime=0
call PAL_EventObjectWrite(358, $event_object)
set g_nSpriteToDrawHighWater=0
call PAL_MakeScene()

printf "CARDPUTER_DRAW_STRESS count=%d high_water=%d capacity=512\n", g_nSpriteToDraw, g_nSpriteToDrawHighWater
if g_nSpriteToDraw != 358
  printf "CARDPUTER_DRAW_STRESS FAIL: expected count 358\n"
  quit 2
end
if g_nSpriteToDrawHighWater != 358
  printf "CARDPUTER_DRAW_STRESS FAIL: expected high-water 358\n"
  quit 3
end
printf "CARDPUTER_DRAW_STRESS PASS\n"
quit 0
