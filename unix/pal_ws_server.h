/*
 * Local WebSocket control endpoint for the Unix/Linux SDL build.
 *
 * The server is disabled unless PAL_WS_PORT is set.  It binds only to
 * 127.0.0.1 and is polled from PAL_ProcessEvent(), so every command executes
 * on the game thread.
 */

#ifndef PAL_WS_SERVER_H
#define PAL_WS_SERVER_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

enum
{
   PAL_WS_KEY_TAP = 0,
   PAL_WS_KEY_DOWN = 1,
   PAL_WS_KEY_UP = 2
};

VOID PAL_WsServer_Init(VOID);
VOID PAL_WsServer_Poll(VOID);
VOID PAL_WsServer_Shutdown(VOID);

/* Host-only interactive UI review mode. It is enabled only by --ui-test. */
VOID PAL_WsReviewParseArgs(INT argc, char *argv[]);
VOID PAL_WsReviewEnable(VOID);
BOOL PAL_WsReviewEnabled(VOID);
VOID PAL_WsReviewNext(VOID);

/* Implemented by input.c so remote input uses the ordinary PAL key state. */
VOID PAL_WsInputKey(DWORD key, INT action);

#ifdef __cplusplus
}
#endif

#endif
