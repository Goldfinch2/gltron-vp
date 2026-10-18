/* gltron.h — GLtron's game data and logic, for the VectorPie port.
 *
 * The structures, constants and declarations of GLtron's game.h, game_data.h,
 * game_level.h, event.h, engine.h and ai.h, unchanged, gathered in one
 * header; then the few functions the port supplies in place of GLtron's
 * Lua, console, audio and video code.
 *
 * GLtron Copyright (C) Andreas Umbach. This port: GPL v2 or later (COPYING).
 */
#ifndef GLTRON_H
#define GLTRON_H

#include <stdio.h>
#include <stdlib.h>

#include "../base/nebu_vector.h"
#include "../base/nebu_util.h"
#include "../base/nebu_math.h"
#include "../base/nebu_random.h"
#include "../base/nebu_assert.h"

/* ---- game_level.h ------------------------------------------------------------ */

enum {
	eGameSpawnUndef,
	eGameSpawnPoint,
	eGameSpawnLine
};

typedef struct game_spawnpoint {
	int type;
	vec2 vStart;
	vec2 vEnd; // unused for eGameSpawnLine
	int n;
	int dir;
} game_spawnpoint;

typedef struct game_spawnset {
	int type;
	int nPoints;
	game_spawnpoint *pSpawnPoints;
} game_spawnset;

typedef struct game_level {
	int nBoundaries;
	segment2 *boundaries;
	int nAxis;
	vec2 *pAxis;
	int nSpawnSets;
	int spawnIsRelative;
	game_spawnset **ppSpawnSets;
	box2 boundingBox;
	float scale_factor;
} game_level;

/* ---- game_data.h --------------------------------------------------------------- */

typedef struct {
	unsigned int current;
	unsigned int lastFrame;
	unsigned int offset;
	unsigned int dt; /* current - lastFrame */
} SystemTime;

#define PLAYER_IS_ACTIVE(x) ((x)->data.speed > 0)

typedef struct RuleSet {
	int eraseCrashed;
	float speed;
} RuleSet;

typedef struct Game2 {
	game_level *level;
	RuleSet rules;
	SystemTime time;
	nebu_List events;
} Game2;

typedef struct Data {
	int dir;

	int score;
	float speed; /* set to -1 when dead */
	int boost_enabled;
	float energy;
	int wall_buster_enabled;
	float trail_height;

	int last_dir;
	/* for cycle animation */
	unsigned int turn_time;
	/* explosion  & impact stuff */
	float impact_radius;
	float exp_radius;

	segment2 *trails; // light cycle trail segments
	int nTrails; // segments currently in use
	int maxTrails; // segments where memory is allocated for
} Data;

typedef struct AI {
	int active;
	int tdiff;
	long lasttime;
	segment2 left, right, front, backleft;
} AI;

typedef struct Player {
	Data data;
	AI ai;
} Player;

typedef struct Game {
	Player *player;
	int players; //!< number of players
	int winner; //!< who won this round
	int pauseflag;
	//!< if the game is finished: the PAUSE_GAME_FINISHED flag is set
	int running; //!< the number of players that are still alive
} Game;

/* ---- event.h ------------------------------------------------------------------------ */

typedef enum {
  EVENT_TURN_LEFT = 1,
  EVENT_TURN_RIGHT = 2,
  EVENT_CRASH = 4,
  EVENT_STOP = 8
} event_type_e;

typedef struct GameEvent {
  int type; /* what */
  int player; /* who */
  float x; /* where */
  float y;
  unsigned int timestamp;
} GameEvent;

void createEvent(int player, event_type_e eventType);
int processEvent(GameEvent *e);

/* ---- game.h ----------------------------------------------------------------------------- */

enum {
	TURN_LEFT = -1,
	TURN_RIGHT = 1
};

enum {
	AI_HUMAN = 0,
	AI_COMPUTER = 1,
	AI_NONE = 2
};

/* physics recomputed every 20ms */
#define PHYSICS_RATE 20

#define MAX_PLAYERS 4
#define INITIAL_TRAIL_COUNT 10

/* these must be < 0 */
#define SPEED_CRASHED -1
#define SPEED_GONE -2

#define TRAIL_HEIGHT 3.5f

/* game options */
#define SPEED_OZ_FREQ 1200
#define SPEED_OZ_FACTOR 0.09f

#define TURN_LENGTH 200

enum {
	PAUSE_NO_GAME = 0,
	PAUSE_GAME_RUNNING = 1,
	PAUSE_GAME_FINISHED = 2,
	PAUSE_GAME_SUSPENDED = 4,
	PAUSE_GAME_STARTING = 8,
};

/* from GLtron's video headers: explosion growth (the logic drives it) */
#define IMPACT_RADIUS_DELTA 0.025f
#define IMPACT_MAX_RADIUS 25.0f
#define EXP_RADIUS_MAX 30
#define EXP_RADIUS_DELTA 0.01f

extern Game *game;
extern Game2 *game2;

void initGameAI(int level);
void doComputer(int player, int target);
void resetScores(void);
void doTurn(GameEvent* e, int direction);
void doMovement(int dt);
int game_ComputeTimeDelta(void);
void Game_Idle(void);
void resetPlayerData(void);
void initLevels(void);
void getPositionFromIndex(float *x, float *y, int player);
void getPositionFromData(float *x, float *y, Data *data);

/* ---- engine.h ------------------------------------------------------------------------------ */

void doCrashPlayer(GameEvent *e);
void game_ResetData(void);
void game_CreatePlayers(int players, Game **ppGame, Game2 **ppGame2);
void game_FreeGame(Game* pGame);
void game_FreeGame2(Game2* pGame2);

/* ---- ai.h ------------------------------------------------------------------------------------ */

typedef struct AI_Configuration {
	int location;
	segment2 player;
	segment2 opponent;
	vec2 intersection;
	float t_player, t_opponent;
} AI_Configuration;

typedef struct AI_Distances {
	float front, left, right, backleft;
} AI_Distances;

#define MAX_AI_LEVEL 3

typedef struct AI_Parameters {
	unsigned int minTurnTime[MAX_AI_LEVEL + 1];
	float maxSegLength[MAX_AI_LEVEL + 1];
	float critical[MAX_AI_LEVEL + 1];
	int  spiral[MAX_AI_LEVEL + 1];
	int rlDelta[MAX_AI_LEVEL + 1];
} AI_Parameters;

extern AI_Parameters ai_params;

void ai_getDistances(int player, AI_Distances *distances);
void ai_getClosestOpponent(int player, int* opponent, float *distance);
void ai_getConfig(int player, int opponent, AI_Configuration *config);
void ai_aggressive(int player, int opponent, int location,
                   AI_Distances *distances, AI_Parameters *pAIParameters);
void ai_evasive(int player, int opponent, int location,
                AI_Distances *distances, AI_Parameters *pAIParameters);

/* ---- supplied by the port ------------------------------------------------------------------- */

/* Settings: GLtron read these from Lua (scripts/config.lua) on every call;
 * here a table of the same defaults (settings.c). */
float getSettingf(const char *name);
int getSettingi(const char *name);
void setSettingf(const char *name, float value);

typedef struct SettingsCache {
	int ai_level;
	int fast_finish;
} SettingsCache;
extern SettingsCache gSettingsCache;

/* The arena (level.c): GLtron's levels/square.lua, as C. */
game_level *game_CreateLevel(void);
void game_FreeLevel(game_level *l);

/* Console messages ("player 2 crashed"): shown on the HUD (main.c). */
enum { TO_CONSOLE = 1, TO_STDOUT = 2 };
void displayMessage(int where, const char *format, ...);

/* Sound (main.c). */
void Audio_CrashPlayer(int player);
void Audio_StopEngine(int player);

#endif /* GLTRON_H */
