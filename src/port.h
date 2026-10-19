/* port.h — what the VectorPie port of GLtron shares between its files.
 * This port: GPL v2 or later (COPYING). */
#ifndef PORT_H
#define PORT_H

#include <vectorpie/vp.h>
#include "game/gltron.h"

/* GLtron's world is right-handed with Z up; the SDK's is left-handed with
 * Y up. Swapping the two axes converts both at once. */
static inline vp_vec3 W(float x, float y, float z) { return vp_v3(x, z, y); }

enum {
	STATE_TITLE,        /* the attract demo, the title and the high scores */
	STATE_READY,        /* the cycles on their spawn points */
	STATE_PLAYING,
	STATE_OVER,         /* a round is over: who won */
	STATE_GAMEOVER,     /* every human is out of lives */
	STATE_ENTRY,        /* a human enters initials for the high scores */
	STATE_CONTROLS      /* laying out the controls (from the title) */
};

/* The functions a player lays out (vp_controls.h), P1's then P2's. */
#define FUNCTIONS 8
extern const char *const FUNCTION_NAME[FUNCTIONS];    /* vp_input's: "p1_turn_left" ... */
extern const char *const FUNCTION_LABEL[FUNCTIONS];   /* on the screen: "P1 TURN LEFT" ... */

#define LIVES 5             /* per human, per game */

typedef struct Port {
	int        state;
	float      stateTime;         /* seconds in this state */
	int        humans;            /* 0 in the attract demo, then 1 or 2 */
	int        lives[2];          /* each human's; at 0 their cycle sits out */
	vp_credits credits;           /* coins in, games paid for */

	vp_score   table[VP_HISCORE_MAX];   /* the high scores, as last loaded */
	int        tableCount;
	int        tableNew;          /* the entry just made (blinks), or -1 */
	int        tableFirst;        /* the title shows the table first */
	vp_initials entry;            /* initials being entered ... */
	int        entryPlayer;       /* ... by this human (0 or 1) */

	vp_input  *input;             /* the controls (vp_game_input) */
	int        ctlRow;            /* CONTROLS: the cursor's row (FUNCTIONS = EXIT) */
	vp_capture capture;           /* ... and a function's controls being laid out */
	char       ctlSaid[64];       /* what the page says under its rows */

	vp3d_mesh *cycle;             /* data/lightcycle-low.obj */
	vp_vec3    cycleMin;          /* its bounding box: stands it on the floor */
	vp3d_mesh *recognizer;        /* data/recognizer_quad.obj */
	float      recAlpha;          /* the recognizer's place on its path */
	float      recReach;          /* its model's horizontal reach from its origin, unscaled */
	vp3d_scene *scene;
	vp3d_view  view;

	char       message[64];       /* the last console message, and its age */
	float      messageTime;

	vp_audio  *audio;
	vp_sound  *sndEngine, *sndCrash, *sndRecognizer, *sndStart, *sndWin, *sndLose, *sndMenu, *sndCoin;
	vp_voice   engine[MAX_PLAYERS];
	vp_voice   recognizerVoice;
} Port;

extern Port P;

/* The attract demo is running: the title, or the CONTROLS page over it. */
static inline int attractDemo(void) { return P.state == STATE_TITLE || P.state == STATE_CONTROLS; }

/* render.c */
void render_frame(vp_dvg *d);
float render_heading(int dir);   /* the SDK rotation (about Y) for a GLtron direction */
void recognizer_position(vec2 *p, vec2 *v);

#endif
