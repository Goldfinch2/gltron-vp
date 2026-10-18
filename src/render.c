/* GLtron for VectorPie — drawing, with the SDK's 3D scene and stroke text.
 *
 * Replaces GLtron's OpenGL renderer (src/video): the floor grid, the arena
 * walls, the trails as solid walls (they hide what is behind them), the light
 * cycles (data/lightcycle-low.obj, solid), the crash explosions, the
 * recognizer flying over the arena, the cameras, and the HUD (scores, map,
 * speed and energy).
 *
 * GLtron Copyright (C) Andreas Umbach. This port: GPL v2 or later (COPYING).
 */
#include "port.h"
#include "wall_art.h"

#include <math.h>
#include <stdio.h>

#define ARENA       600.0f          /* levels/square.lua scale_factor */
#define WALL_H      75.0f           /* square.lua: 0.125 x scale_factor */
#define GRID_STEP   30.0f
#define REC_HEIGHT  50.0f           /* video.h RECOGNIZER_HEIGHT */
#define REC_SCALE   0.25f           /* recognizer.c rec_scale_factor */
#define LEAN_DEG    25.0f           /* gamegraphics.c "neigung" */

/* GLtron's artpack.lua colors, brightened for a vector monitor */
static const uint8_t CYCLE_RGB[MAX_PLAYERS][3] = {
	{ 255, 140,  36 }, { 255,  40,  40 }, {  40, 170, 255 }, { 220, 220, 220 } };
static const uint8_t TRAIL_RGB[MAX_PLAYERS][3] = {
	{ 255, 217,  36 }, { 230,  30,  30 }, {  30, 150, 220 }, { 180, 180, 180 } };

static void color(vp_dvg *d, const uint8_t *rgb, float k)
{
	if(k < 0) k = 0;
	if(k > 1) k = 1;
	vp_color(d, (uint8_t)(rgb[0] * k), (uint8_t)(rgb[1] * k), (uint8_t)(rgb[2] * k));
}

/* ---- angles (camera.c) ------------------------------------------------------------------ */

/* GLtron's getAngle: the angle of the direction pointing backwards. */
static float getAngle(int dir)
{
	vec2 *v = &game2->level->pAxis[dir];
	float a = (float) atan2(-v->v[1], -v->v[0]);
	if(a < 0) a += 2 * PI;
	return a;
}

static float getInterpolatedAngle(float t, float phiLast, float phiNow)
{
	if(phiLast > phiNow && phiNow + 2 * PI - phiLast < phiLast - phiNow)
		phiNow += 2 * PI;
	if(phiNow > phiLast && phiLast + 2 * PI - phiNow < phiNow - phiLast)
		phiLast += 2 * PI;
	return (1 - t) * phiLast + t * phiNow;
}

/* The cycle's heading as a rotation about the SDK's Y axis: the model faces
 * +Y in GLtron, +Z after loading; a GLtron direction (vx, vy) is (vx, 0, vy). */
float render_heading(int dir)
{
	vec2 *v = &game2->level->pAxis[dir];
	return (float) atan2(v->v[0], v->v[1]);
}

static float headingNow(Data *data)
{
	unsigned int t = game2->time.current - data->turn_time;
	float now = render_heading(data->dir);
	float last, diff;
	if(t >= TURN_LENGTH || data->last_dir == data->dir)
		return now;
	last = render_heading(data->last_dir);
	diff = now - last;                         /* the short way round */
	while(diff > PI) diff -= 2 * PI;
	while(diff < -PI) diff += 2 * PI;
	return last + diff * (float) t / TURN_LENGTH;
}

/* ---- the recognizer's path (recognizer.c) ---------------------------------------------- */

static const float xv[] = { 0.5f, 0.3245f, 0.6f, 0.5f, 0.68f, -0.3f };
static const float yv[] = { 0.8f, 1.0f, 0.0f, 0.2f, 0.2f, 0.0f };

/* GLtron's curve, but scaled to stay inside the walls: GLtron sized it from
 * the arena's diagonal, which let the recognizer fly through the walls. Each
 * axis's two terms add up to at most 1 (0.5 + 0.5, 0.8 + 0.2), so an
 * amplitude of `half` keeps the center within `half` of the arena's center;
 * the model's reach and a margin are kept clear of the walls. */
void recognizer_position(vec2 *p, vec2 *v)
{
	float a = P.recAlpha;
	const box2 *b = &game2->level->boundingBox;
	float room = fminf(box2_Width(b), box2_Height(b)) / 2;
	float half = room - P.recReach * REC_SCALE - 5.0f;
	if(half < 0) half = 0;
	box2_Center(p, b);
	p->v[0] += (xv[0] * sinf(xv[1] * a + xv[2]) - xv[3] * sinf(xv[4] * a + xv[5])) * half;
	p->v[1] += (yv[0] * cosf(yv[1] * a + yv[2]) - yv[3] * sinf(yv[4] * a + yv[5])) * half;
	v->v[0] = (xv[1] * xv[0] * cosf(xv[1] * a + xv[2]) - xv[4] * xv[3] * cosf(xv[4] * a + xv[5])) * half / 50.f;
	v->v[1] = (-yv[1] * yv[0] * sinf(yv[1] * a + yv[2]) - yv[4] * yv[3] * sinf(yv[4] * a + yv[5])) * half / 50.f;
}

/* ---- the camera ------------------------------------------------------------------------- */

static void placeCamera(void)
{
	vp3d_view *v = &P.view;
	/* One human: GLtron's default follow camera ("Mike-cam"), alive or
	 * still exploding (it stays on the crash, pulling back as the explosion
	 * grows; then the whole-arena view below). */
	if(P.humans == 1 && P.state != STATE_TITLE &&
	   (game->player[0].data.speed > 0 || game->player[0].data.exp_radius < EXP_RADIUS_MAX)) {
		Data *data = &game->player[0].data;
		float x, y, phi, chi = PI / 3, r = 18.0f + 1.5f * data->exp_radius;
		unsigned int t = game2->time.current - data->turn_time;
		getPositionFromData(&x, &y, data);
		phi = t < TURN_LENGTH
			? getInterpolatedAngle(1 - (float)(TURN_LENGTH - t) / TURN_LENGTH,
			                       getAngle(data->last_dir), getAngle(data->dir))
			: getAngle(data->dir);
		v->fov_deg = 100.0f;
		/* depth cue: far lines down to 6 % */
		v->fog_start = 40.0f;
		v->fog_end = 500.0f;
		v->fog_floor = 0.06f;
		vp3d_look_at(v, W(x + r * cosf(phi) * sinf(chi), y + r * sinf(phi) * sinf(chi), r * cosf(chi)),
		             W(x, y, 0), vp_v3(0, 1, 0));
		return;
	}
	/* Two humans, the attract demo, or a crashed player: the whole arena,
	 * from above one side (circling slowly in the demo). */
	v->fov_deg = 70.0f;
	v->fog_floor = 0.06f;               /* near side of the arena full, far side 6 % */
	if(P.state == STATE_TITLE) {
		v->fog_start = 450.0f;          /* the camera is about 775 from the center */
		v->fog_end = 1000.0f;
		float a = P.recAlpha * 0.15f;
		vp3d_look_at(v, W(ARENA / 2 + 650 * sinf(a), ARENA / 2 - 650 * cosf(a), 420),
		             W(ARENA / 2, ARENA / 2, 0), vp_v3(0, 1, 0));
	} else {
		v->fog_start = 580.0f;          /* the near wall, then the far wall about 1000 */
		v->fog_end = 950.0f;
		vp3d_look_at(v, W(ARENA / 2, -260, 520), W(ARENA / 2, ARENA * 0.55f, 0), vp_v3(0, 1, 0));
	}
}

/* ---- the world ---------------------------------------------------------------------------- */

static void drawWallArt(vp_dvg *d);

static void drawArena(vp_dvg *d)
{
	int i;
	/* the floor: a grid of lines (GLtron's was a texture) */
	vp_color(d, 0, 0, 255);
	for(i = 0; i <= (int)(ARENA / GRID_STEP); i++) {
		float c = i * GRID_STEP;
		vp3d_scene_line(P.scene, W(c, 0, 0), W(c, ARENA, 0));
		vp3d_scene_line(P.scene, W(0, c, 0), W(ARENA, c, 0));
	}
	/* the arena walls: solid, outlined */
	vp_color(d, 120, 0, 200);
	for(i = 0; i < game2->level->nBoundaries; i++) {
		segment2 *b = game2->level->boundaries + i;
		float x0 = b->vStart.v[0], y0 = b->vStart.v[1];
		float x1 = x0 + b->vDirection.v[0], y1 = y0 + b->vDirection.v[1];
		vp_vec3 q[4] = { W(x0, y0, 0), W(x1, y1, 0), W(x1, y1, WALL_H), W(x0, y0, WALL_H) };
		vp3d_scene_polygon(P.scene, q, 4, VP3D_ALL_EDGES);
	}
	drawWallArt(d);
}

/* GLtron's wall texture, traced into lines (wall_art.h). The strip wraps
 * once around the arena, a quarter per wall, starting at the corner (0, 0)
 * and going up the x = 0 wall (levels/square.lua's texture coordinates);
 * the top of the image at the top of the wall. Lines on a wall are seen
 * from inside the arena, and hidden behind trails and cycles like the rest. */
static void wallArtPoint(int u, int v, float *x, float *y, float *z)
{
	static const float corner[5][2] = { {0, 0}, {0, ARENA}, {ARENA, ARENA}, {ARENA, 0}, {0, 0} };
	float t = u * 4.0f / WALL_ART_WIDTH;
	int q = (int)t;
	if(q > 3) q = 3;
	t -= q;
	*x = corner[q][0] + (corner[q + 1][0] - corner[q][0]) * t;
	*y = corner[q][1] + (corner[q + 1][1] - corner[q][1]) * t;
	*z = WALL_H * (1.0f - (float)v / (WALL_ART_HEIGHT - 1));
}

static void drawWallArt(vp_dvg *d)
{
	int i, j;
	const int quarter = WALL_ART_WIDTH / 4;
	vp_color(d, 120, 0, 200);
	for(i = 0; i < WALL_ART_POLYLINES; i++) {
		for(j = wall_art_start[i] + 1; j < wall_art_start[i + 1]; j++) {
			const unsigned short *a = wall_art_points[j - 1], *b = wall_art_points[j];
			int lo = a[0] < b[0] ? a[0] : b[0], hi = a[0] < b[0] ? b[0] : a[0];
			float ax, ay, az, bx, by, bz;
			/* a segment may end on a corner, but one crossing it would cut across */
			if(hi > lo && lo / quarter != (hi - 1) / quarter) continue;
			wallArtPoint(a[0], a[1], &ax, &ay, &az);
			wallArtPoint(b[0], b[1], &bx, &by, &bz);
			vp3d_scene_line(P.scene, W(ax, ay, az), W(bx, by, bz));
		}
	}
}

/* Each trail segment is a wall: a solid polygon that hides what is behind it,
 * drawn as its top and bottom lines and a post where it starts (the corners). */
static void drawTrails(vp_dvg *d)
{
	int i, k;
	for(i = 0; i < game->players; i++) {
		Data *data = &game->player[i].data;
		float h = data->trail_height;
		if(h <= 0) continue;
		color(d, TRAIL_RGB[i], h / TRAIL_HEIGHT);
		for(k = 0; k < data->nTrails; k++) {
			segment2 *s = data->trails + k;
			float x0 = s->vStart.v[0], y0 = s->vStart.v[1];
			float x1 = x0 + s->vDirection.v[0], y1 = y0 + s->vDirection.v[1];
			unsigned mask = 1u << 0 | 1u << 2 | 1u << 3;     /* bottom, top, start post */
			if(fabsf(x1 - x0) + fabsf(y1 - y0) < 0.01f) continue;
			if(k == data->nTrails - 1) mask |= 1u << 1;       /* and the end post */
			{
				vp_vec3 q[4] = { W(x0, y0, 0), W(x1, y1, 0), W(x1, y1, h), W(x0, y0, h) };
				vp3d_scene_polygon(P.scene, q, 4, mask);
			}
		}
	}
}

static void drawCycles(vp_dvg *d)
{
	int i;
	for(i = 0; i < game->players; i++) {
		Data *data = &game->player[i].data;
		float x, y, lean = 0;
		unsigned int t = game2->time.current - data->turn_time;
		vp_xf place;
		if(data->speed <= 0) continue;
		getPositionFromData(&x, &y, data);
		/* lean into a turn (gamegraphics.c getCycleTransformation) */
		if(t < TURN_LENGTH && data->last_dir != data->dir) {
			int left = (data->dir - data->last_dir + game2->level->nAxis) % game2->level->nAxis
			           == game2->level->nAxis - 1;
			lean = (left ? 1 : -1) * LEAN_DEG * sinf(PI * t / TURN_LENGTH) * PI / 180.0f;
		}
		place = vp_xf_mul2(vp_xf_translate(x, -P.cycleMin.y, y),
		        vp_xf_mul2(vp_xf_rot_y(headingNow(data) + PI), vp_xf_rot_z(lean)));   /* the model faces -Y */
		/* GLtron draws the hull black while the wall buster is on: dim here */
		color(d, CYCLE_RGB[i], data->wall_buster_enabled ? 0.35f : 1.0f);
		vp3d_scene_mesh(P.scene, P.cycle, &place, VP3D_SOLID);
	}
}

/* explosion.c: shock-wave rings and spires, growing and fading */
static void drawExplosions(vp_dvg *d)
{
	int i, j, k;
	for(i = 0; i < game->players; i++) {
		Data *data = &game->player[i].data;
		float x, y, r = data->exp_radius, fade;
		if(data->speed > 0 || r <= 0 || r >= EXP_RADIUS_MAX) continue;
		getPositionFromData(&x, &y, data);
		fade = 1.0f - r / EXP_RADIUS_MAX;
		vp_color(d, (uint8_t)(255 * fade), (uint8_t)(90 * fade), (uint8_t)(20 * fade));
		for(k = 1; k <= 3; k++) {                  /* three rings */
			float rr = r * k / 3.0f;
			for(j = 0; j < 24; j++) {
				float a0 = 2 * PI * j / 24, a1 = 2 * PI * (j + 1) / 24;
				vp3d_scene_line(P.scene, W(x + rr * cosf(a0), y + rr * sinf(a0), 0.5f),
				                         W(x + rr * cosf(a1), y + rr * sinf(a1), 0.5f));
			}
		}
		for(j = 0; j < 12; j++) {                  /* spires */
			float a = 2 * PI * j / 12;
			vp3d_scene_line(P.scene, W(x, y, 0),
			                W(x + 0.6f * r * cosf(a), y + 0.6f * r * sinf(a), 0.8f * r));
		}
	}
}

static void drawRecognizer(vp_dvg *d)
{
	vec2 p, v;
	vp_xf place;
	recognizer_position(&p, &v);
	place = vp_xf_mul2(vp_xf_translate(p.v[0], REC_HEIGHT, p.v[1]),
	        vp_xf_mul2(vp_xf_rot_y((float) atan2(v.v[0], v.v[1]) + PI / 2),
	                   vp_xf_scale(REC_SCALE, REC_SCALE, REC_SCALE)));
	vp_color(d, 255, 0, 0);
	vp3d_scene_mesh(P.scene, P.recognizer, &place, VP3D_SOLID);
}

/* ---- the HUD ------------------------------------------------------------------------------- */

static void drawMap(vp_dvg *d)
{
	/* visuals_2d.c: the arena and every trail, from above */
	const float x0 = 3250, y0 = 150, size = 700, k = size / ARENA;
	int i, j;
	vp_color(d, 0, 90, 200);
	vp_moveto(d, (int)x0, (int)y0);
	vp_lineto(d, (int)(x0 + size), (int)y0);
	vp_lineto(d, (int)(x0 + size), (int)(y0 + size));
	vp_lineto(d, (int)x0, (int)(y0 + size));
	vp_lineto(d, (int)x0, (int)y0);
	for(i = 0; i < game->players; i++) {
		Data *data = &game->player[i].data;
		if(data->trail_height <= 0) continue;
		color(d, TRAIL_RGB[i], data->trail_height / TRAIL_HEIGHT);
		for(j = 0; j < data->nTrails; j++) {
			segment2 *s = data->trails + j;
			if(j == 0)
				vp_moveto(d, (int)(x0 + s->vStart.v[0] * k), (int)(y0 + s->vStart.v[1] * k));
			vp_lineto(d, (int)(x0 + (s->vStart.v[0] + s->vDirection.v[0]) * k),
			             (int)(y0 + (s->vStart.v[1] + s->vDirection.v[1]) * k));
		}
	}
}

static void drawBar(vp_dvg *d, float x, float y, float w, float value)
{
	if(value < 0) value = 0;
	if(value > 1) value = 1;
	vp_moveto(d, (int)x, (int)y);
	vp_lineto(d, (int)(x + w * value), (int)y);
}

/* A human's lives left: a small chevron each. */
static void drawLives(vp_dvg *d, float x, float y, int n)
{
	int k;
	for(k = 0; k < n; k++) {
		float cx = x + k * 80.0f;
		vp_moveto(d, (int)cx, (int)y);
		vp_lineto(d, (int)(cx + 30), (int)(y + 50));
		vp_lineto(d, (int)(cx + 60), (int)y);
	}
}

static void drawHUD(vp_dvg *d)
{
	char text[64];
	int i;
	vp_text_font(VP_FONT_ASTEROIDS);

	/* scores, in each player's color */
	for(i = 0; i < game->players; i++) {
		const char *who = i < P.humans ? (i == 0 ? "P1" : "P2") : "CPU";
		snprintf(text, sizeof text, "%s %d", who, game->player[i].data.score);
		color(d, CYCLE_RGB[i], game->player[i].data.speed > 0 ? 1.0f : 0.4f);
		vp_text(d, text, 250 + i * 950.0f, 3880, 90, VP_TEXT_LEFT);
		if(i < P.humans)
			drawLives(d, 250 + i * 950.0f, 3740, P.lives[i]);
	}

	/* speed and energy, for the humans still in the game */
	for(i = 0; i < P.humans; i++) {
		Data *data = &game->player[i].data;
		float y = 250 + i * 330.0f;
		color(d, CYCLE_RGB[i], 1.0f);
		vp_text(d, i == 0 ? "P1" : "P2", 150, y, 90, VP_TEXT_LEFT);
		if(P.lives[i] <= 0) {
			vp_text(d, "OUT", 450, y, 90, VP_TEXT_LEFT);
			continue;
		}
		vp_color(d, 120, 255, 120);
		drawBar(d, 450, y + 90, 900, data->speed / (2 * game2->rules.speed));
		vp_color(d, 255, 220, 60);
		drawBar(d, 450, y, 900, data->energy / getSettingf("energy"));
	}

	drawMap(d);

	/* the last console message, for two seconds */
	if(P.messageTime < 2.0f && P.message[0] && P.state != STATE_TITLE) {
		vp_color(d, 160, 160, 160);
		vp_text(d, P.message, 2048, 950, 70, VP_TEXT_CENTER);
	}
}

/* The attract screens' prompt, blinking: INSERT COIN, or which START can
 * be pressed for the credits there are; and the credits, at the bottom. */
static void drawCoinPrompt(vp_dvg *d, float y, float height)
{
	char text[32];
	const char *prompt = vp_credits_enough(&P.credits, 2) ? "PRESS 1 OR 2 PLAYER START"
	                   : vp_credits_enough(&P.credits, 1) ? "PRESS 1 PLAYER START"
	                   : "INSERT COIN";
	vp_color(d, 255, 255, 255);
	if((int)(P.stateTime * 2) % 2 == 0)
		vp_text(d, prompt, 2048, y, height, VP_TEXT_CENTER);
	if(!P.credits.free_play) {
		snprintf(text, sizeof text, "CREDITS %d", P.credits.credits);
		vp_text(d, text, 2048, 130, 80, VP_TEXT_CENTER);
	}
}

static void drawCenterText(vp_dvg *d)
{
	char text[64];
	switch(P.state) {
	case STATE_TITLE:
		vp_text_font(VP_FONT_ASTEROIDS);
		/* every 14 seconds: the title for 8, the high scores for 6 (the
		 * high scores first right after a new entry) */
		if(P.tableCount > 0 && fmodf(P.stateTime + (P.tableFirst ? 8.0f : 0.0f), 14.0f) >= 8.0f) {
			vp_color(d, 0, 200, 255);
			vp_text(d, "HIGH SCORES", 2048, 3200, 160, VP_TEXT_CENTER);
			vp_color(d, 255, 255, 255);
			vp_hiscore_draw(d, P.table, P.tableCount, 2048, 2850, 100, P.tableNew, P.stateTime);
			drawCoinPrompt(d, 1050, 90);
			break;
		}
		vp_color(d, 0, 200, 255);
		vp_text(d, "GLTRON", 2048, 2700, 380, VP_TEXT_CENTER);
		drawCoinPrompt(d, 2350, 110);
		vp_text_font(VP_FONT_HERSHEY);
		vp_color(d, 140, 140, 140);
		vp_text(d, "GLtron by Andreas Umbach - VectorPie port", 2048, 2150, 70, VP_TEXT_CENTER);
		break;
	case STATE_READY:
		vp_text_font(VP_FONT_ASTEROIDS);
		vp_color(d, 255, 255, 255);
		vp_text(d, "GET READY", 2048, 2300, 220, VP_TEXT_CENTER);
		break;
	case STATE_OVER:
		vp_text_font(VP_FONT_ASTEROIDS);
		if(game->winner >= 0 && game->winner < P.humans)
			snprintf(text, sizeof text, "PLAYER %d WINS", game->winner + 1);
		else if(game->winner >= 0)
			snprintf(text, sizeof text, "CPU WINS");
		else
			snprintf(text, sizeof text, "NO WINNER");
		vp_color(d, 255, 255, 255);
		vp_text(d, text, 2048, 2300, 220, VP_TEXT_CENTER);
		/* the lives left */
		if(P.humans == 1)
			snprintf(text, sizeof text, P.lives[0] == 1 ? "LAST LIFE" : "%d LIVES LEFT", P.lives[0]);
		else
			snprintf(text, sizeof text, "LIVES  P1 %d   P2 %d", P.lives[0], P.lives[1]);
		vp_text(d, text, 2048, 2000, 110, VP_TEXT_CENTER);
		break;
	case STATE_GAMEOVER:
		vp_text_font(VP_FONT_ASTEROIDS);
		vp_color(d, 255, 80, 40);
		vp_text(d, "GAME OVER", 2048, 2300, 300, VP_TEXT_CENTER);
		vp_color(d, 255, 255, 255);
		if(P.humans == 1)
			snprintf(text, sizeof text, "SCORE %d", game->player[0].data.score);
		else
			snprintf(text, sizeof text, "P1 %d   P2 %d",
			         game->player[0].data.score, game->player[1].data.score);
		vp_text(d, text, 2048, 1950, 130, VP_TEXT_CENTER);
		break;
	case STATE_ENTRY:
		vp_text_font(VP_FONT_ASTEROIDS);
		color(d, CYCLE_RGB[P.entryPlayer], 1.0f);
		snprintf(text, sizeof text, "PLAYER %d", P.entryPlayer + 1);
		vp_text(d, text, 2048, 2950, 150, VP_TEXT_CENTER);
		vp_color(d, 255, 255, 255);
		snprintf(text, sizeof text, "HIGH SCORE %d", game->player[P.entryPlayer].data.score);
		vp_text(d, text, 2048, 2700, 120, VP_TEXT_CENTER);
		vp_text(d, "ENTER YOUR INITIALS", 2048, 2450, 100, VP_TEXT_CENTER);
		vp_initials_draw(&P.entry, d, 2048, 1950, 260);
		vp_text_font(VP_FONT_HERSHEY);
		vp_color(d, 160, 160, 160);
		vp_text(d, "stick up/down: letter   button 1: next   button 2: back", 2048, 1550, 70, VP_TEXT_CENTER);
		break;
	default:
		break;
	}
}

void render_frame(vp_dvg *d)
{
	placeCamera();
	vp3d_scene_begin(P.scene, d, &P.view);
	drawArena(d);
	drawTrails(d);
	drawCycles(d);
	drawExplosions(d);
	drawRecognizer(d);
	vp3d_scene_end(P.scene);

	drawHUD(d);
	drawCenterText(d);
}
