/* The arena: GLtron's levels/square.lua, as C, scaled as GLtron scaled it
 * (scale_factor 600: a 600 x 600 square). Only what the game logic uses:
 * boundaries (collisions), axis (the directions a cycle can face) and spawn
 * points. The floor and arena meshes were for OpenGL; the port draws its own.
 * GLtron Copyright (C) Andreas Umbach. This port: GPL v2 or later. */

#include "gltron.h"

#include <string.h>

#define SCALE 600.0f

/* directions = { random = -1, up = 2, down = 0, right = 1, left = 3 } */
enum { DIR_RANDOM = -1, DIR_DOWN = 0, DIR_RIGHT = 1, DIR_UP = 2, DIR_LEFT = 3 };

static void setSegment(segment2 *s, float x0, float y0, float x1, float y1)
{
	s->vStart.v[0] = x0 * SCALE;
	s->vStart.v[1] = y0 * SCALE;
	s->vDirection.v[0] = (x1 - x0) * SCALE;
	s->vDirection.v[1] = (y1 - y0) * SCALE;
}

static game_spawnpoint point(float x, float y, int dir)
{
	game_spawnpoint p;
	memset(&p, 0, sizeof(p));
	p.type = eGameSpawnPoint;
	p.vStart.v[0] = x;
	p.vStart.v[1] = y;
	p.dir = dir;
	return p;
}

static game_spawnpoint line(float x0, float y0, float x1, float y1, int n, int dir)
{
	game_spawnpoint p;
	memset(&p, 0, sizeof(p));
	p.type = eGameSpawnLine;
	p.vStart.v[0] = x0;
	p.vStart.v[1] = y0;
	p.vEnd.v[0] = x1;
	p.vEnd.v[1] = y1;
	p.n = n;
	p.dir = dir;
	return p;
}

game_level *game_CreateLevel(void)
{
	game_level *l = (game_level*) calloc(1, sizeof(game_level));
	game_spawnset *set;

	l->scale_factor = SCALE;
	l->spawnIsRelative = 1;

	/* collision detection takes place against these lines */
	l->nBoundaries = 4;
	l->boundaries = (segment2*) malloc(4 * sizeof(segment2));
	setSegment(&l->boundaries[0], 0, 0, 1, 0);
	setSegment(&l->boundaries[1], 1, 0, 1, 1);
	setSegment(&l->boundaries[2], 1, 1, 0, 1);
	setSegment(&l->boundaries[3], 0, 1, 0, 0);
	l->boundingBox.vMin.v[0] = 0;
	l->boundingBox.vMin.v[1] = 0;
	l->boundingBox.vMax.v[0] = SCALE;
	l->boundingBox.vMax.v[1] = SCALE;

	/* the four directions, in turning order */
	l->nAxis = 4;
	l->pAxis = (vec2*) malloc(4 * sizeof(vec2));
	l->pAxis[0].v[0] = 0;  l->pAxis[0].v[1] = -1;
	l->pAxis[1].v[0] = -1; l->pAxis[1].v[1] = 0;
	l->pAxis[2].v[0] = 0;  l->pAxis[2].v[1] = 1;
	l->pAxis[3].v[0] = 1;  l->pAxis[3].v[1] = 0;

	/* spawn points, relative to the bounding box */
	l->nSpawnSets = 2;
	l->ppSpawnSets = (game_spawnset**) malloc(2 * sizeof(game_spawnset*));

	set = (game_spawnset*) malloc(sizeof(game_spawnset));
	set->type = eGameSpawnPoint;
	set->nPoints = 4;
	set->pSpawnPoints = (game_spawnpoint*) malloc(4 * sizeof(game_spawnpoint));
	set->pSpawnPoints[0] = point(.5f, .75f, DIR_DOWN);
	set->pSpawnPoints[1] = point(.5f, .25f, DIR_UP);
	set->pSpawnPoints[2] = point(.25f, .5f, DIR_RANDOM);
	set->pSpawnPoints[3] = point(.75f, .5f, DIR_RANDOM);
	l->ppSpawnSets[0] = set;

	set = (game_spawnset*) malloc(sizeof(game_spawnset));
	set->type = eGameSpawnLine;
	set->nPoints = 2;
	set->pSpawnPoints = (game_spawnpoint*) malloc(2 * sizeof(game_spawnpoint));
	set->pSpawnPoints[0] = line(.25f, .25f, .75f, .25f, 999, DIR_UP);
	set->pSpawnPoints[1] = line(.25f, .75f, .75f, .75f, 999, DIR_DOWN);
	l->ppSpawnSets[1] = set;

	return l;
}

void game_FreeLevel(game_level *l)
{
	int i;
	if(!l) return;
	for(i = 0; i < l->nSpawnSets; i++) {
		free(l->ppSpawnSets[i]->pSpawnPoints);
		free(l->ppSpawnSets[i]);
	}
	free(l->ppSpawnSets);
	free(l->pAxis);
	free(l->boundaries);
	free(l);
}
