/* GLtron for VectorPie — the game flow, the controls and the sound.
 *
 * GLtron's own game logic (src/game) runs unchanged at its 20 ms physics
 * tick: vp_run calls update() 50 times a second. This file replaces what
 * GLtron did in Lua and SDL: the menus (a title screen with an attract demo),
 * rounds, controls (the cabinet's, through vp_input) and sound (vp_audio).
 * render.c draws.
 *
 * Controls, per player (P1, P2): left / right turn, button 1 boost, button 2
 * wall buster; COIN adds a credit; START 1 / START 2 begin a 1 or 2 player
 * game for 1 or 2 credits, their lamps blinking on the title screen when
 * there are enough. The exit button goes back to the VectorPie menu.
 *
 * The players' controls are GLtron's own functions (vp_controls.h): out of the
 * box the cabinet's (P1 LEFT turns P1 left ...), changed on the CONTROLS
 * page, which TAB, or START 1 and START 2 together, opens from the title.
 * They are kept in save/controls.ini.
 *
 * A game (not in GLtron, which played rounds forever): each human has 5
 * lives and loses one in each round they crash in; a human out of lives sits
 * out; when all are out, GAME OVER. Scores are GLtron's (a point for every
 * cycle that crashes while you are still riding), kept across the rounds; a
 * score that makes the top 10 is entered with initials (vp_hiscore) and
 * saved in save/highscores.txt (the format the VectorPie menu reads). The title
 * screen alternates with the table.
 *
 * GLtron Copyright (C) Andreas Umbach. This port: GPL v2 or later (COPYING).
 */
#include "port.h"

#include <stdarg.h>
#include <string.h>

Game *game;
Game2 *game2;
Port P;

#define TICK_MS     20      /* = vp_run's update step at 50 Hz = GLtron's PHYSICS_RATE */
#define CYCLES      4       /* humans + AI */
#define READY_TIME  2.0f
#define OVER_TIME   3.5f
#define GAMEOVER_TIME 4.0f
#define ENTRY_LIMIT 30.0f   /* seconds to enter initials */

/* ---- the console and sound hooks the game logic calls --------------------- */

void displayMessage(int where, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	vsnprintf(P.message, sizeof(P.message), format, ap);
	va_end(ap);
	P.messageTime = 0;
	if(where & TO_STDOUT) printf("gltron: %s\n", P.message);
}

/* GLtron played crashes in 3D. Here: a human's own crash at full volume; a
 * computer player's quieter the farther it is from the camera; everything
 * softer in the attract demo. */
#define CRASH_HUMAN     0.9f
#define CRASH_CPU_MAX   0.5f
#define CRASH_CPU_MIN   0.08f
#define CRASH_RANGE     500.0f      /* arena units: from here on, the minimum */

void Audio_CrashPlayer(int player)
{
	float vol;
	if(player < P.humans) {
		vol = CRASH_HUMAN;
	} else {
		float x, y, d;
		vp_vec3 eye = vp3d_eye(&P.view);
		getPositionFromIndex(&x, &y, player);
		d = vp_v3_length(vp_v3_sub(W(x, y, 0), eye));
		vol = CRASH_CPU_MAX * (1.0f - d / CRASH_RANGE);
		if(vol < CRASH_CPU_MIN) vol = CRASH_CPU_MIN;
	}
	if(attractDemo()) vol *= 0.25f;
	vp_sound_play(P.audio, P.sndCrash, vol, 1.0f, 0);
}

/* GLtron played the recognizer's sound in 3D: loud only when it flies close
 * to the camera. Here: its volume falls with distance. */
#define REC_SOUND_MAX    0.3f
#define REC_SOUND_RANGE  350.0f     /* arena units: silent beyond this */

static void recognizerSound(void)
{
	vec2 p, v;
	vp_vec3 eye, rec;
	float dist, vol;
	if(!P.recognizerVoice) return;
	recognizer_position(&p, &v);
	eye = vp3d_eye(&P.view);
	rec = W(p.v[0], p.v[1], 50.0f);
	dist = vp_v3_length(vp_v3_sub(rec, eye));
	vol = REC_SOUND_MAX * (1.0f - dist / REC_SOUND_RANGE);
	vp_voice_set_volume(P.audio, P.recognizerVoice, vol > 0 ? vol : 0, 0.1f);
}

void Audio_StopEngine(int player)
{
	if(player >= 0 && player < MAX_PLAYERS && P.engine[player]) {
		vp_voice_stop(P.audio, P.engine[player], 0.15f);
		P.engine[player] = 0;
	}
}

static void stopEngines(void)
{
	int i;
	for(i = 0; i < MAX_PLAYERS; i++) Audio_StopEngine(i);
}

/* ---- rounds ------------------------------------------------------------------------ */

/* A new game: `humans` 0 (the attract demo), 1 or 2; the AI fills the arena. */
static void newGame(int humans)
{
	P.humans = humans;
	setSettingf("ai_opponents", (float)(CYCLES - humans));
	/* Fast finish speeds the game up (25x here) once no human is left
	 * alive, so the computer players end the round quickly. The attract demo
	 * has no humans at all: it must run at normal speed. */
	setSettingf("fast_finish", humans > 0 ? 1.0f : 0.0f);
	game_CreatePlayers(CYCLES, &game, &game2);
	initLevels();
	resetScores();
}

/* A human out of lives sits the rest of the game out: no cycle, no trail
 * (as GLtron's AI_NONE players). */
static void sitOut(int i)
{
	Data *data = &game->player[i].data;
	game->player[i].ai.active = AI_NONE;
	data->speed = SPEED_GONE;
	data->trail_height = 0;
	data->exp_radius = EXP_RADIUS_MAX;
	game->running--;
}

/* A new round, scores kept: the cycles on their spawn points, waiting. */
static void newRound(void)
{
	int i;
	stopEngines();
	game_ResetData();
	for(i = 0; i < P.humans; i++)
		if(P.lives[i] <= 0) sitOut(i);
}

/* ... and off they go. */
static void beginRound(void)
{
	int i;
	game->pauseflag = PAUSE_GAME_RUNNING;
	for(i = 0; i < P.humans; i++)
		if(P.lives[i] > 0)
			P.engine[i] = vp_sound_play(P.audio, P.sndEngine, 0.35f, 1.0f, 1);
}

/* A round is over: every human who crashed in it loses a life. Returns the
 * lives left, all humans together. */
static int loseLives(void)
{
	int i, left = 0;
	for(i = 0; i < P.humans; i++) {
		if(P.lives[i] > 0 && game->player[i].data.speed <= 0)
			P.lives[i]--;
		left += P.lives[i];
	}
	return left;
}

static void loadTable(void)
{
	P.tableCount = vp_hiscore_load(P.table);
}

/* The attract demo: four computer players, the title and the high scores. */
static void goTitle(int tableFirst)
{
	stopEngines();
	newGame(0);
	newRound();
	beginRound();
	loadTable();
	P.tableFirst = tableFirst;
	P.state = STATE_TITLE;
	P.stateTime = 0;
}

static void setState(int state)
{
	P.state = state;
	P.stateTime = 0;
}

static void startGame(int humans)
{
	vp_sound_play(P.audio, P.sndMenu, 0.8f, 1.0f, 0);
	P.lives[0] = P.lives[1] = LIVES;
	P.tableNew = -1;
	newGame(humans);
	newRound();                /* the cycles wait on their spawn points */
	vp_sound_play(P.audio, P.sndStart, 0.8f, 1.0f, 0);
	setState(STATE_READY);
}

/* ---- the high scores ---------------------------------------------------------------- */

/* After the game: each human whose score makes the table enters initials,
 * player 1 first; then the title, showing the table. */
static void nextEntry(int from)
{
	int i;
	for(i = from; i < P.humans; i++) {
		if(vp_hiscore_rank(game->player[i].data.score) >= 0) {
			P.entryPlayer = i;
			vp_initials_begin(&P.entry, i + 1, ENTRY_LIMIT);
			setState(STATE_ENTRY);
			return;
		}
	}
	goTitle(P.tableNew >= 0);
}

static void entryDone(void)
{
	int r = vp_hiscore_insert(P.entry.name, game->player[P.entryPlayer].data.score);
	if(r >= 0)
		P.tableNew = r;
	else if(r < -1)
		fprintf(stderr, "gltron: high score not saved: %s\n", vp_last_error());
	nextEntry(P.entryPlayer + 1);
}

/* ---- the controls -------------------------------------------------------------------- */

#define KEY_TAB        43           /* SDL scancodes */
#define KEY_DELETE     76
#define KEY_KP_PERIOD  99

const char *const FUNCTION_NAME[FUNCTIONS] = {
	"p1_turn_left", "p1_turn_right", "p1_boost", "p1_wall_buster",
	"p2_turn_left", "p2_turn_right", "p2_boost", "p2_wall_buster",
};
const char *const FUNCTION_LABEL[FUNCTIONS] = {
	"P1 TURN LEFT", "P1 TURN RIGHT", "P1 BOOST", "P1 WALL BUSTER",
	"P2 TURN LEFT", "P2 TURN RIGHT", "P2 BOOST", "P2 WALL BUSTER",
};
/* Out of the box: the cabinet's. */
static const char *const FUNCTION_STANDARD[FUNCTIONS] = {
	"p1_left", "p1_right", "p1_button1", "p1_button2",
	"p2_left", "p2_right", "p2_button1", "p2_button2",
};

static void defineControls(vp_input *in)
{
	int i;
	for(i = 0; i < FUNCTIONS; i++)
		vp_input_define(in, FUNCTION_NAME[i], FUNCTION_STANDARD[i]);
	vp_input_reserve_key(in, KEY_TAB);      /* opens and closes the CONTROLS page */
	vp_input_load(in, NULL);                /* the players' own, if they set any */
}

static void saveControls(vp_input *in)
{
	if(vp_input_save(in, NULL) != VP_OK)
		fprintf(stderr, "gltron: controls not saved: %s\n", vp_last_error());
}

static void goControls(void)
{
	vp_sound_play(P.audio, P.sndMenu, 0.8f, 1.0f, 0);
	memset(&P.capture, 0, sizeof P.capture);
	P.ctlRow = 0;
	P.ctlSaid[0] = 0;
	setState(STATE_CONTROLS);
}

static void leaveControls(void)
{
	vp_sound_play(P.audio, P.sndMenu, 0.8f, 1.0f, 0);
	P.state = STATE_TITLE;          /* the attract demo goes on */
	P.stateTime = 0;
}

/* The CONTROLS page: Up/Down a row, START 1 / Enter lays out its function's
 * controls (vp_capture), Left/Right none or standard, Delete standard; EXIT
 * or TAB back to the title. */
static void controlsPage(vp_input *in)
{
	const char *said;
	int left, right;
	if(P.capture.capturing) {
		if(vp_capture_step(&P.capture, in) == VP_CAPTURE_CHANGED) {
			saveControls(in);
			vp_sound_play(P.audio, P.sndMenu, 0.8f, 1.0f, 0);
		}
	} else if(vp_input_key_pressed(in, KEY_TAB)) {
		leaveControls();
		return;
	} else {
		if(vp_input_menu_up(in) || vp_input_menu_down(in)) {
			P.ctlRow = (P.ctlRow + (vp_input_menu_up(in) ? FUNCTIONS : 1)) % (FUNCTIONS + 1);
			vp_capture_moved(&P.capture);
			vp_sound_play(P.audio, P.sndCoin, 0.5f, 1.0f, 0);
		}
		left = vp_input_menu_left_pressed(in);
		right = vp_input_menu_right_pressed(in);
		vp_capture_filter(&P.capture, &left, &right);
		if(P.ctlRow < FUNCTIONS) {
			const char *name = FUNCTION_NAME[P.ctlRow];
			if(vp_input_menu_confirm(in)) {
				vp_capture_begin(&P.capture, in, name);
			} else if(left || right) {
				vp_capture_toggle(&P.capture, in, name);
				saveControls(in);
			} else if(vp_input_key_pressed(in, KEY_DELETE) || vp_input_key_pressed(in, KEY_KP_PERIOD)) {
				vp_capture_standard(&P.capture, in, name);
				saveControls(in);
			}
		} else if(vp_input_menu_confirm(in)) {
			leaveControls();
			return;
		}
	}
	said = vp_capture_said(&P.capture);
	snprintf(P.ctlSaid, sizeof P.ctlSaid, "%s", said ? said : "");
}

static void humanControls(vp_input *in)
{
	static const char *const turnLeft[2]  = { "p1_turn_left", "p2_turn_left" };
	static const char *const turnRight[2] = { "p1_turn_right", "p2_turn_right" };
	static const char *const boost[2]     = { "p1_boost", "p2_boost" };
	static const char *const buster[2]    = { "p1_wall_buster", "p2_wall_buster" };
	int i;

	for(i = 0; i < P.humans; i++) {
		Data *data = &game->player[i].data;
		if(!PLAYER_IS_ACTIVE(&game->player[i]))
			continue;
		/* Turns on the press only: holding a direction does not spin. */
		if(vp_input_pressed(in, turnLeft[i]))  createEvent(i, EVENT_TURN_LEFT);
		if(vp_input_pressed(in, turnRight[i])) createEvent(i, EVENT_TURN_RIGHT);

		/* Booster and wall buster: while held, if there is energy to start
		 * (GLtron's input.c rules). */
		if(vp_input_pressed(in, boost[i]) && getSettingi("booster_on") &&
		   data->energy > getSettingf("booster_min"))
			data->boost_enabled = 1;
		if(vp_input_released(in, boost[i]))
			data->boost_enabled = 0;
		if(vp_input_pressed(in, buster[i]) && getSettingi("wall_buster_on") &&
		   data->energy > getSettingf("wall_buster_min"))
			data->wall_buster_enabled = 1;
		if(vp_input_released(in, buster[i]))
			data->wall_buster_enabled = 0;
	}
}

/* ---- one game tick -------------------------------------------------------------------- */

static void tick(void)
{
	int i;
	game2->time.lastFrame = game2->time.current;
	game2->time.current += TICK_MS;
	game2->time.dt = TICK_MS;
	Game_Idle();

	/* the engines sound faster with the cycles */
	for(i = 0; i < P.humans; i++) {
		if(P.engine[i]) {
			float s = game->player[i].data.speed / game2->rules.speed;
			vp_voice_set_pitch(P.audio, P.engine[i], 0.7f + 0.3f * (s > 2 ? 2 : s));
		}
	}
}

/* The round ends as soon as every human has crashed (once their explosions
 * are over): no watching the computer players play it out. GLtron itself
 * played every round to the last cycle. The winner is then a surviving
 * computer player, if there is one. */
static int humansOut(void)
{
	int i, j;
	if(P.humans == 0) return 0;
	for(i = 0; i < P.humans; i++) {
		Data *data = &game->player[i].data;
		if(data->speed > 0 || data->exp_radius < EXP_RADIUS_MAX)
			return 0;
	}
	game->winner = -1;
	for(j = P.humans; j < game->players; j++)
		if(game->player[j].data.speed > 0) { game->winner = j; break; }
	game->pauseflag = PAUSE_GAME_FINISHED;
	return 1;
}

static int humanWon(void)
{
	return game->winner >= 0 && game->winner < P.humans;
}

static void update(vp_game *g, float dt)
{
	vp_input *in = vp_game_input(g);
	P.stateTime += dt;
	P.messageTime += dt;
	P.recAlpha += dt * 1000.0f / 2000.0f;      /* GLtron: alpha += dt / 2000 (ms) */
	recognizerSound();

	/* Coins count at any time; START lamps blink on the title screen when
	 * there are credits enough for that START, and are off otherwise. */
	if(vp_credits_update(&P.credits, in))
		vp_sound_play(P.audio, P.sndCoin, 0.8f, 1.0f, 0);
	if(P.state == STATE_TITLE)
		vp_credits_blink(&P.credits, vp_game_time(g));
	else
		vp_lamps_set(0);

	/* START 1 / START 2 begin a game from the title, for 1 or 2 credits;
	 * TAB, or both together, opens the CONTROLS page instead. */
	if(P.state == STATE_TITLE) {
		if(vp_input_key_pressed(in, KEY_TAB) ||
		   (vp_input_held(in, "start1") && vp_input_held(in, "start2") &&
		    (vp_input_pressed(in, "start1") || vp_input_pressed(in, "start2")))) {
			goControls();
			return;
		}
		if(vp_input_pressed(in, "start1") && vp_credits_take(&P.credits, 1)) { startGame(1); return; }
		if(vp_input_pressed(in, "start2") && vp_credits_take(&P.credits, 2)) { startGame(2); return; }
	}

	switch(P.state) {
	case STATE_TITLE:
		/* the attract demo: four AI cycles, a new round as soon as one ends */
		if(game->pauseflag == PAUSE_GAME_FINISHED) {
			newRound();
			beginRound();
		}
		tick();
		break;

	case STATE_READY:
		if(P.stateTime >= READY_TIME) {
			beginRound();
			setState(STATE_PLAYING);
		}
		break;

	case STATE_PLAYING:
		humanControls(in);
		tick();
		if(game->pauseflag == PAUSE_GAME_FINISHED || humansOut()) {
			stopEngines();
			vp_sound_play(P.audio, humanWon() ? P.sndWin : P.sndLose, 0.8f, 1.0f, 0);
			setState(loseLives() > 0 ? STATE_OVER : STATE_GAMEOVER);
		}
		break;

	case STATE_OVER:
		if(P.stateTime >= OVER_TIME) {
			newRound();
			vp_sound_play(P.audio, P.sndStart, 0.8f, 1.0f, 0);
			setState(STATE_READY);
		}
		break;

	case STATE_GAMEOVER:
		if(P.stateTime >= GAMEOVER_TIME)
			nextEntry(0);
		break;

	case STATE_ENTRY:
		if(vp_initials_update(&P.entry, in, dt))
			entryDone();
		break;

	case STATE_CONTROLS:
		/* the attract demo goes on behind the page */
		if(game->pauseflag == PAUSE_GAME_FINISHED) {
			newRound();
			beginRound();
		}
		tick();
		controlsPage(in);
		break;
	}
}

static void draw(vp_game *g, vp_dvg *d)
{
	(void)g;
	render_frame(d);
}

/* ---- start and end ---------------------------------------------------------------------- */

static vp_sound *loadSound(const char *name)
{
	char path[1024];
	vp_sound *s = NULL;
	if(P.audio && vp_sound_load(P.audio, vp_asset_path(name, path, sizeof path), &s) != VP_OK)
		fprintf(stderr, "gltron: %s\n", vp_last_error());
	return s;
}

static int init(vp_game *g)
{
	char path[1024];
	vp3d_obj_options opt;
	vp_vec3 min, max;

	memset(&opt, 0, sizeof opt);
	opt.z_up = 1;                       /* GLtron's models are Z-up */
	if(vp3d_mesh_load_obj(vp_asset_path("data/lightcycle-low.obj", path, sizeof path), &opt, &P.cycle) != VP_OK)
		return VP_ERR_NOTFOUND;            /* vp_run prints vp_last_error() */
	vp3d_mesh_bounds(P.cycle, &P.cycleMin, &max);
	if(vp3d_mesh_load_obj(vp_asset_path("data/recognizer_quad.obj", path, sizeof path), &opt, &P.recognizer) != VP_OK)
		return VP_ERR_NOTFOUND;
	vp3d_mesh_bounds(P.recognizer, &min, &max);
	P.recReach = sqrtf(fmaxf(min.x * min.x, max.x * max.x) + fmaxf(min.z * min.z, max.z * max.z));
	if(vp3d_scene_create(&P.scene) != VP_OK)
		return VP_ERR_NOMEM;

	P.audio = vp_game_audio(g);
	P.sndEngine     = loadSound("data/sounds/game_engine.wav");
	P.sndCrash      = loadSound("data/sounds/game_crash.wav");
	P.sndRecognizer = loadSound("data/sounds/game_recognizer.wav");
	P.sndStart      = loadSound("data/sounds/game_start.wav");
	P.sndWin        = loadSound("data/sounds/game_win.wav");
	P.sndLose       = loadSound("data/sounds/game_lose.wav");
	P.sndMenu       = loadSound("data/sounds/menu_action.wav");
	P.sndCoin       = loadSound("data/sounds/menu_highlight.wav");
	vp_credits_init(&P.credits);
	P.recognizerVoice = vp_sound_play(P.audio, P.sndRecognizer, 0.0f, 1.0f, 1);   /* volume: recognizerSound */

	/* GLtron's soundtrack, looped from the title screen on, as in GLtron */
	if(P.audio && !vp_music_play(P.audio, vp_asset_path("data/music/revenge_of_cats.ogg", path, sizeof path),
	                             0.4f, 1, 2.0f))
		fprintf(stderr, "gltron: %s\n", vp_last_error());
	nebu_srand((unsigned) vp_ticks_ms());
	vp3d_view_init(&P.view, 100.0f);
	P.view.near_z = 0.5f;               /* GLtron's znear */
	P.view.far_z = 4000.0f;

	P.input = vp_game_input(g);
	defineControls(P.input);

	P.tableNew = -1;
	goTitle(0);                         /* the attract demo */
	return VP_OK;
}

static void shutdown(vp_game *g)
{
	(void)g;
	vp3d_scene_free(P.scene);
	vp3d_mesh_free(P.cycle);
	vp3d_mesh_free(P.recognizer);
}

int main(int argc, char **argv)
{
	vp_game_desc desc;
	memset(&desc, 0, sizeof desc);
	desc.title     = "GLtron";
	desc.init      = init;
	desc.update    = update;
	desc.draw      = draw;
	desc.shutdown  = shutdown;
	desc.update_hz = 1000 / TICK_MS;    /* 50: one GLtron physics tick per step */
	desc.frame_hz_board = 30;           /* the board gets more time per frame (40 in a window) */
	return vp_run(&desc, argc, argv);
}
