#include "arenaScreen.h"

#include "../Graphics/graphics.h"
#include "../Graphics/images.h"
#include "../Graphics/spineGfx.h"
#include "../Graphics/camera.h"
#include "../Graphics/debugRendering.h"
#include "../Graphics/imageSheets.h"
#include "../Graphics/sprites.h"
#include "../Utils/hexGrid.h"
#include "values.h"
#include "../Utils/stretchyBuffer.h"
#include "../UI/text.h"
#include "../System/platformLog.h"
#include "../System/random.h"
#include "../Utils/helpers.h"
#include "../tween.h"
#include "../Utils/aStar.h"
#include "../sound.h"

// you have cells, a CWxCH grid is a spot, a SWxSH grid is an arena
#define CELL_WIDTH 3
#define CELL_HEIGHT 3
#define SPOT_WIDTH 3
#define SPOT_HEIGHT 3
#define ARENA_WIDTH 3
#define ARENA_HEIGHT 3

#define MAP_WIDTH ( CELL_WIDTH * SPOT_WIDTH * ARENA_WIDTH )
#define MAP_HEIGHT ( CELL_HEIGHT * SPOT_HEIGHT * ARENA_HEIGHT )

typedef enum {
	IT_NORMAL,
	IT_OVERDRIVE,
	IT_LOOK,
	IT_AIM,
	IT_HELP,
	IT_WON,
	IT_LOST,
	IT_INTRO,
	IT_REPAIR
} InputType;

static int lockedToMoveTurns = 0;
static int aimBonusTurns = 0;
static bool useOverdriveShot = false;
static InputType inputType;

static bool isVisible( HexGridCoord lookFrom, HexGridCoord target, int32_t maxDist );
static Vector2 getDrawPos( HexGridCoord coord );
static bool isMoveable( HexGridCoord coord );
static bool doesTerrainBlock( HexGridCoord coord );
static void startPlayerAction( size_t idx );
static void aq_PushToEndOfQueue( size_t objIdx );
static void standardEnemyAI( size_t idx );
static void standardEnemyRangedAttack( size_t idx );
static void standardEnemyMeleeAttack( size_t idx );
static void deathExplosion( size_t idx );
static void alarmEnemyAI( size_t idx );
static void createLevel( void );

static bool isActionRunning;
static bool isPlayerActing;

static bool drawHelp;
static bool drawIntro;

static int repairPointsLeft;

static int level;

static bool compareCoords( HexGridCoord* lhs, HexGridCoord* rhs )
{
	return ( lhs->q == rhs->q ) && ( lhs->r == rhs->r );
}

typedef enum {
	TT_GROUND,
	TT_WATER,
	TT_TREE_CENTER,
	TT_TREE_N,
	TT_TREE_NE,
	TT_TREE_SE,
	TT_TREE_S,
	TT_TREE_SW,
	TT_TREE_NW
} TileType;

typedef struct {
	int smokeLeft;
	TileType type;

	int img;

	bool isVisible;
} MapTile;

static MapTile map[MAP_WIDTH * MAP_HEIGHT];

#define DICE_SIDES 6
#define DICE_SUCCESS 4
static int rollDicePool( int dice )
{
	if( dice <= 0 ) {
		return 0;
	}

	int s = 0;
	for( int i = 0; i < dice; ++i ) {
		if( rand_GetRangeS32( NULL, 1, DICE_SIDES ) >= DICE_SUCCESS ) {
			++s;
		}
	}

	return s;
}

#define MOVE_ANIM_TIME 0.15f

float soundTimeOut = 0.0f;

static void playSound( int sound )
{
	if( sound < 0 ) return;
	if( soundTimeOut > 0.0f ) return;

	snd_Play( sound, 0.75f, 1.0f, 0.0f, 0 );
	soundTimeOut = 0.25f;
}

//******************************************
// Game Objects
typedef enum {
	OS_PLAYER,
	OS_IDLE,
	OS_CURIOUS,
	OS_AGGRESIVE,
	OS_DEAD
} ObjectState;

typedef enum {
	AT_IDLE,
	AT_MOVE,
	AT_ATTACK,
	AT_FLY,
	AT_SHAKE
} AnimType;

typedef struct {
	const char* name;
	const char* desc;
	int health;
	int maxHealth;

	ObjectState state;
	HexGridCoord curiousSpot;

	int aliveImg;
	int deadImg;

	HexGridCoord pos;
	HexGridCoord targetPos;
	float posT;
	AnimType animType;

	uint8_t depth;

	int dodgeDice;
	int armorDice;
	int hitDice;
	int damageDice;
	int attackRange;

	int counter;

	int sightRange;

	size_t shakeOnDeathObj;

	bool destroyOnDoneAnim;
	int animDoneSound;

	int immunityTurns;

	bool breakOnThis;

	void (*action)( size_t );
	void (*attackAction)( size_t );
	void (*deathAction)( size_t );

	bool needsToDie;
} Object;

#define PLAYER_OBJ_IDX 0

#define MAX_OBJECTS 64

static Object* sbObjects = NULL;

static void clearObjects( )
{
	sb_Clear( sbObjects );

	// create player placeholder
	Object emptyObj = { NULL };
	sb_Push( sbObjects, emptyObj );
}

static Object defaultObj( void )
{
	Object obj;

	obj.shakeOnDeathObj = SIZE_MAX;
	obj.destroyOnDoneAnim = false;
	obj.immunityTurns = 0;
	obj.counter = 0;
	obj.sightRange = -1;
	obj.breakOnThis = false;
	obj.attackRange = 1;
	obj.attackAction = NULL;
	obj.deathAction = NULL;
	obj.needsToDie = false;
	obj.animDoneSound = -1;

	return obj;
}

static void createPlayerObject( HexGridCoord startPos )
{
	Object playerObj = defaultObj( );

	playerObj.name = "You";
	playerObj.desc = "You";
	playerObj.state = OS_PLAYER;

	playerObj.aliveImg = playerImg;
	playerObj.deadImg = playerDeadImg;

	playerObj.pos = startPos;
	playerObj.targetPos = startPos;
	playerObj.posT = MOVE_ANIM_TIME;

	playerObj.action = startPlayerAction;

	playerObj.dodgeDice = 0;
	playerObj.armorDice = 0;
	playerObj.hitDice = 0;
	playerObj.damageDice = 0;

	playerObj.health = 10;
	playerObj.maxHealth = 10;

	playerObj.depth = 100;
	playerObj.destroyOnDoneAnim = false;

	aimBonusTurns = 0;

	sbObjects[PLAYER_OBJ_IDX] = playerObj;
}

static void createSmallTreeObject( HexGridCoord pos )
{
	Object treeObj = defaultObj( );

	treeObj.name = "Small Tree";
	treeObj.desc = "Small fragile tree.\nCan be broken.";
	treeObj.state = OS_IDLE;

	treeObj.aliveImg = smallTreeImg;
	treeObj.deadImg = smallTreeDeadImg;

	treeObj.health = treeObj.maxHealth = 8;

	treeObj.pos = pos;
	treeObj.targetPos = pos;
	treeObj.posT = MOVE_ANIM_TIME;

	treeObj.action = NULL;

	treeObj.dodgeDice = 0;
	treeObj.armorDice = 2;
	treeObj.hitDice = 0;
	treeObj.damageDice = 0;

	treeObj.depth = 10;

	treeObj.counter = 0;
	treeObj.destroyOnDoneAnim = false;

	sb_Push( sbObjects, treeObj );
}

static void createProjectileObject( HexGridCoord start, HexGridCoord end, size_t target, int animDoneSound )
{
	Object projObj = defaultObj( );

	projObj.name = "Projectile";
	projObj.desc = "Projectile";
	projObj.state = OS_IDLE;

	projObj.aliveImg = projectileImg;
	projObj.deadImg = projectileImg;

	projObj.health = projObj.maxHealth = 8;

	projObj.pos = start;
	projObj.targetPos = end;
	projObj.posT = 0.0f;
	projObj.animType = AT_FLY;

	projObj.action = NULL;

	projObj.dodgeDice = 0;
	projObj.armorDice = 2;
	projObj.hitDice = 0;
	projObj.damageDice = 0;

	projObj.depth = 110;

	projObj.counter = 0;
	projObj.destroyOnDoneAnim = true;
	projObj.animDoneSound = animDoneSound;

	projObj.shakeOnDeathObj = target;

	sb_Push( sbObjects, projObj );
}

static void createWeakEnemy( HexGridCoord pos )
{
	Object obj = defaultObj( );;

	obj.name = "Phot";
	obj.desc = "Large reptile.\nWeak but show up in\ngroups.";

	obj.aliveImg = weakNmyImg;
	obj.deadImg = weakNmyDeadImg;

	obj.pos = pos;
	obj.targetPos = pos;
	obj.posT = MOVE_ANIM_TIME;
	obj.animType = AT_IDLE;

	obj.health = obj.maxHealth = 8;
	obj.dodgeDice = 6;
	obj.armorDice = 1;
	obj.hitDice = 12;
	obj.damageDice = 8;

	obj.action = standardEnemyAI;
	obj.attackAction = standardEnemyMeleeAttack;

	obj.depth = 100;

	obj.state = OS_IDLE;
	obj.counter = rand_GetRangeS32( NULL, 0, 3 );

	obj.sightRange = 20;

	obj.needsToDie = true;

	//obj.breakOnThis = true;

	sb_Push( sbObjects, obj );
}

static void createStrongEnemy( HexGridCoord pos )
{
	Object obj = defaultObj( );;

	obj.name = "Othokep";
	obj.desc = "Very large mammal.\nStrong and\ndangerous.";

	obj.aliveImg = strongNmyImg;
	obj.deadImg = strongNmyDeadImg;

	obj.pos = pos;
	obj.targetPos = pos;
	obj.posT = MOVE_ANIM_TIME;
	obj.animType = AT_IDLE;

	obj.health = obj.maxHealth = 8;
	obj.dodgeDice = 6;
	obj.armorDice = 1;
	obj.hitDice = 12;
	obj.damageDice = 12;

	obj.action = standardEnemyAI;
	obj.attackAction = standardEnemyMeleeAttack;

	obj.depth = 100;

	obj.state = OS_IDLE;
	obj.counter = rand_GetRangeS32( NULL, 0, 3 );

	obj.sightRange = 20;

	obj.needsToDie = true;

	//obj.breakOnThis = true;

	sb_Push( sbObjects, obj );
}

static void createArmoredEnemy( HexGridCoord pos )
{
	Object obj = defaultObj( );;

	obj.name = "Dhos";
	obj.desc = "Shelled creature.\nHard to kill.\nHits hard.";

	obj.aliveImg = armorNmyImg;
	obj.deadImg = armorNmyDeadImg;

	obj.pos = pos;
	obj.targetPos = pos;
	obj.posT = MOVE_ANIM_TIME;
	obj.animType = AT_IDLE;

	obj.health = obj.maxHealth = 8;
	obj.dodgeDice = 6;
	obj.armorDice = 10;
	obj.hitDice = 10;
	obj.damageDice = 8;

	obj.action = standardEnemyAI;
	obj.attackAction = standardEnemyMeleeAttack;

	obj.depth = 100;

	obj.state = OS_IDLE;
	obj.counter = rand_GetRangeS32( NULL, 0, 3 );

	obj.sightRange = 20;

	obj.needsToDie = true;

	//obj.breakOnThis = true;

	sb_Push( sbObjects, obj );
}

static void createRangedEnemy( HexGridCoord pos )
{
	Object obj = defaultObj( );

	obj.name = "Pho-lar";
	obj.desc = "Attacks from range.";

	obj.aliveImg = rangeNmyImg;
	obj.deadImg = rangeNmyDeadImg;

	obj.pos = pos;
	obj.targetPos = pos;
	obj.posT = MOVE_ANIM_TIME;
	obj.animType = AT_IDLE;

	obj.health = obj.maxHealth = 8;
	obj.dodgeDice = 6;
	obj.armorDice = 1;
	obj.hitDice = 12;
	obj.damageDice = 6;

	obj.action = standardEnemyAI;
	obj.attackAction = standardEnemyRangedAttack;
	obj.attackRange = 10;

	obj.depth = 100;

	obj.state = OS_IDLE;
	obj.counter = rand_GetRangeS32( NULL, 0, 3 );

	obj.sightRange = 20;

	obj.needsToDie = true;

	sb_Push( sbObjects, obj );
}

static void createExplosiveEnemy( HexGridCoord pos )
{
	Object obj = defaultObj( );

	obj.name = "Bbor";
	obj.desc = "Explodes on death.";

	obj.aliveImg = boomerNmyImg;
	obj.deadImg = boomerNmyDeadImg;

	obj.pos = pos;
	obj.targetPos = pos;
	obj.posT = MOVE_ANIM_TIME;
	obj.animType = AT_IDLE;

	obj.health = obj.maxHealth = 4;
	obj.dodgeDice = 6;
	obj.armorDice = 2;
	obj.hitDice = 8;
	obj.damageDice = 12;

	obj.action = standardEnemyAI;
	obj.attackAction = standardEnemyMeleeAttack;
	obj.attackRange = 1;
	obj.deathAction = deathExplosion;

	obj.depth = 100;

	obj.state = OS_IDLE;
	obj.counter = rand_GetRangeS32( NULL, 0, 3 );

	obj.sightRange = 20;

	obj.needsToDie = true;

	sb_Push( sbObjects, obj );
}

static void createAlarmEnemy( HexGridCoord pos )
{
	Object obj = defaultObj( );

	obj.name = "Keki";
	obj.desc = "Plant creature.\nNon-hostile but will\nshriek, drawing\nattention.";

	obj.aliveImg = alarmNmyImg;
	obj.deadImg = alarmNmyDeadImg;

	obj.pos = pos;
	obj.targetPos = pos;
	obj.posT = MOVE_ANIM_TIME;
	obj.animType = AT_IDLE;

	obj.health = obj.maxHealth = 4;
	obj.dodgeDice = 0;
	obj.armorDice = 10;
	obj.hitDice = 0;
	obj.damageDice = 0;

	obj.action = alarmEnemyAI;
	obj.attackAction = NULL;
	obj.attackRange = 25;

	obj.depth = 100;

	obj.state = OS_IDLE;
	obj.counter = rand_GetRangeS32( NULL, 0, 3 );

	obj.sightRange = 5;

	sb_Push( sbObjects, obj );
}

static Vector2 getObjectDrawPos( size_t idx )
{
	Vector2 oldPos = getDrawPos( sbObjects[idx].pos );
	Vector2 newPos = getDrawPos( sbObjects[idx].targetPos );

	Vector2 drawPos;
	float t = 1.0f;
	if( sbObjects[idx].animType == AT_MOVE ) {
		t = easeOutQuad( sbObjects[idx].posT / MOVE_ANIM_TIME );
	} else if( sbObjects[idx].animType == AT_ATTACK ) {
		t = easeInOutCirc( sbObjects[idx].posT / MOVE_ANIM_TIME );
		if( t > 0.5f ) {
			t = 1.0f - t;
		}
	} else if( sbObjects[idx].animType == AT_FLY ) {
		t = sbObjects[idx].posT / MOVE_ANIM_TIME;
	} else if( sbObjects[idx].animType == AT_SHAKE ) {
		Vector2 dir;
		vec2_FromPolar( rand_GetRangeFloat( NULL, 0.0f, M_TWO_PI_F ), 1.0f, &dir );
		vec2_AddScaled( &newPos, &dir, 5.0f, &newPos );
		t = 1.0f - ( sbObjects[idx].posT / MOVE_ANIM_TIME );
	}
	vec2_Lerp( &oldPos, &newPos, t, &drawPos );

	return drawPos;
}

static void drawObjects( )
{
	// if the object is visible from the players position then draw them
	for( size_t i = 0; i < sb_Count( sbObjects ); ++i ) {
		int mapIdx = hex_Flat_CoordToRectIndex( sbObjects[i].pos, MAP_WIDTH, MAP_HEIGHT );
		if( !map[mapIdx].isVisible ) {
			continue;
		}

		Vector2 drawPos = getObjectDrawPos( i );

		int img = sbObjects[i].aliveImg;
		uint8_t depth = sbObjects[i].depth;
		if( sbObjects[i].state == OS_DEAD ) {
			img = sbObjects[i].deadImg;
			depth -= 1;
		}

		img_Draw( img, GAME_CAMERA_FLAGS, drawPos, drawPos, depth );

		if( sbObjects[i].immunityTurns > 0 ) {
			img_Draw( shieldImg, GAME_CAMERA_FLAGS, drawPos, drawPos, depth );
		}
	}
}

static size_t getObjectAt( HexGridCoord coord, bool mustBeAlive )
{
	bool foundAlive = false;
	size_t found = SIZE_MAX;
	// find the first live object, otherwise return whatever
	for( size_t i = 0; ( i < sb_Count( sbObjects ) ) && !foundAlive; ++i ) {
		if( compareCoords( &sbObjects[i].pos, &coord ) ) {
			if( mustBeAlive && ( sbObjects[i].health <= 0 ) ) {
				continue;
			}

			if( sbObjects[i].health > 0 ) {
				foundAlive = false;
			}
			found = i;
		}
	}

	return found;
}

static void moveObjecTTo( size_t objIdx, HexGridCoord newPos )
{
	int oldMapIdx = hex_Flat_CoordToRectIndex( sbObjects[objIdx].pos, MAP_WIDTH, MAP_HEIGHT );
	int newMapIdx = hex_Flat_CoordToRectIndex( newPos, MAP_WIDTH, MAP_HEIGHT );
	if( ( objIdx != PLAYER_OBJ_IDX ) && !map[oldMapIdx].isVisible && !map[newMapIdx].isVisible ) {
		// not the player and not visible, just skip
		sbObjects[objIdx].targetPos = newPos;
		sbObjects[objIdx].pos = newPos;
		sbObjects[objIdx].posT = MOVE_ANIM_TIME;
		return;
	}

	sbObjects[objIdx].targetPos = newPos;
	sbObjects[objIdx].posT = 0.0f;
	sbObjects[objIdx].animType = AT_MOVE;
}

static void meleeAttackWithObj( size_t objIdx, HexGridCoord targetPos )
{
	int oldMapIdx = hex_Flat_CoordToRectIndex( sbObjects[objIdx].pos, MAP_WIDTH, MAP_HEIGHT );
	int newMapIdx = hex_Flat_CoordToRectIndex( targetPos, MAP_WIDTH, MAP_HEIGHT );
	if( ( objIdx != PLAYER_OBJ_IDX ) && !map[oldMapIdx].isVisible && !map[newMapIdx].isVisible ) {
		// not the player and not visible, just skip
		sbObjects[objIdx].posT = MOVE_ANIM_TIME;
		return;
	}

	sbObjects[objIdx].targetPos = targetPos;
	sbObjects[objIdx].posT = 0.0f;
	sbObjects[objIdx].animType = AT_ATTACK;
}

static void shakeObject( size_t objIdx )
{
	sbObjects[objIdx].animType = AT_SHAKE;
	sbObjects[objIdx].posT = 0.0f;
}

static bool animateObjects( float dt )
{
	bool allDone = true;
	for( size_t i = 0; i < sb_Count( sbObjects ); ++i ) {
		if( sbObjects[i].posT < MOVE_ANIM_TIME ) {
			sbObjects[i].posT += dt;
			if( sbObjects[i].posT >= MOVE_ANIM_TIME ) {

				if( sbObjects[i].animType == AT_MOVE ) {
					sbObjects[i].pos = sbObjects[i].targetPos;
				} else if( sbObjects[i].animType == AT_ATTACK ) {
					sbObjects[i].targetPos = sbObjects[i].pos;
				} else if( sbObjects[i].animType == AT_FLY ) {
					sbObjects[i].pos = sbObjects[i].targetPos;
				}

				sbObjects[i].animType = AT_IDLE;

				if( sbObjects[i].animDoneSound >= 0 ) {
					playSound( sbObjects[i].animDoneSound );
				}

				if( sbObjects[i].destroyOnDoneAnim ) {
					if( sbObjects[i].shakeOnDeathObj != SIZE_MAX ) {
						shakeObject( sbObjects[i].shakeOnDeathObj );
					}
					sb_Remove( sbObjects, i );
					--i;
				}
			}
			allDone = false;
		}
	}

	return allDone;
}

static void checkForDeath( void )
{
	for( size_t i = 0; i < sb_Count( sbObjects ); ++i ) {
		if( ( sbObjects[i].health <= 0 ) && ( sbObjects[i].state != OS_DEAD ) ) {
			sbObjects[i].state = OS_DEAD;
			if( sbObjects[i].deathAction != NULL ) {
				sbObjects[i].deathAction( i );
			}
		}
	}
}

static void generalObjectTurnEnd( size_t objIdx, bool addAction )
{
	if( sbObjects[objIdx].immunityTurns > 0 ) {
		--sbObjects[objIdx].immunityTurns;
	}

	if( addAction ) {
		aq_PushToEndOfQueue( objIdx );
	}
}

//************************************************************
// Action Queue
static size_t* sbActionQueue = NULL;

static void aq_Clear( void )
{
	sb_Clear( sbActionQueue );
}

static void aq_PushToEndOfQueue( size_t objIdx )
{
	sb_Push( sbActionQueue, objIdx );
}

static void aq_PushToStartOfQueue( size_t objIdx )
{
	sb_Insert( sbActionQueue, 0, objIdx );
}

static size_t aq_GetNextObjIdx( void )
{
	assert( sb_Count( sbActionQueue ) > 0 );

	size_t idx = sbActionQueue[0];
	sb_Remove( sbActionQueue, 0 );

	return idx;
}

//************************************************************
// Map

static bool isMoveableType( TileType type )
{
	switch( type ) {
	case TT_GROUND:
		return true;
	}

	return false;
}

static bool isOuterTree( TileType type )
{
	switch( type ) {
	case TT_TREE_N:
	case TT_TREE_NE:
	case TT_TREE_SE:
	case TT_TREE_S:
	case TT_TREE_SW:
	case TT_TREE_NW:
		return true;
	}

	return false;
}

static bool isBlockingType( TileType type )
{
	switch( type ) {
	case TT_TREE_CENTER:
	case TT_TREE_N:
	case TT_TREE_NE:
	case TT_TREE_SE:
	case TT_TREE_S:
	case TT_TREE_SW:
	case TT_TREE_NW:
		return true;
	}

	return false;
}

static void setMapImages( )
{
	for( int i = 0; i < ( MAP_WIDTH * MAP_HEIGHT ); ++i ) {
		switch( map[i].type ) {
		case TT_GROUND:
			map[i].img = groundTile;
			break;
		case TT_WATER:
			map[i].img = waterTile;
			break;
		case TT_TREE_CENTER:
			map[i].img = treeC;
			break;
		case TT_TREE_N:
			map[i].img = treeN;
			break;
		case TT_TREE_NE:
			map[i].img = treeNE;
			break;
		case TT_TREE_SE:
			map[i].img = treeSE;
			break;
		case TT_TREE_S:
			map[i].img = treeS;
			break;
		case TT_TREE_SW:
			map[i].img = treeSW;
			break;
		case TT_TREE_NW:
			map[i].img = treeNW;
			break;
		default:
			assert( "Unknown tile type" );
			break;
		}
	}
}

#define LAKE_CHANCE 0.5f
#define LAKE_BORDER_EMPTY_CHANCE 0.5f
#define MIN_BIG_TREES 1
#define MAX_BIG_TREES 3
#define MIN_SMALL_TREES 3
#define MAX_SMALL_TREES 5

#define SPOT_TILE_MIN_X( x ) ( (x) * ( SPOT_WIDTH * CELL_WIDTH ) )
#define SPOT_TILE_MAX_X( x ) ( SPOT_TILE_MIN_X( (x) + 1 ) - 1 )
#define SPOT_TILE_MIN_Y( y ) ( (y) * ( SPOT_HEIGHT * CELL_HEIGHT ) )
#define SPOT_TILE_MAX_Y( y ) ( SPOT_TILE_MIN_Y( (y) + 1 ) - 1 )

#define CELL_TILE_MIN_X( sx, cx ) ( SPOT_TILE_MIN_X( (sx) ) + ( (cx) * CELL_WIDTH ) )
#define CELL_TILE_MAX_X( sx, cx ) ( CELL_TILE_MIN_X( (sx), (cx) + 1 ) - 1 )
#define CELL_TILE_MIN_Y( sy, cy ) ( SPOT_TILE_MIN_Y( (sy) ) + ( (cy) * CELL_HEIGHT ) )
#define CELL_TILE_MAX_Y( sy, cy ) ( CELL_TILE_MIN_Y( (sy), (cy) + 1 ) - 1 )

static HexGridCoord getRandomTileInSpot( int spotX, int spotY )
{
	int leftBorder = SPOT_TILE_MIN_X( spotX );
	int rightBorder = SPOT_TILE_MAX_X( spotX );
	int topBorder = SPOT_TILE_MIN_Y( spotY );
	int bottomBorder = SPOT_TILE_MAX_Y( spotY );

	int x = rand_GetRangeS32( NULL, leftBorder, rightBorder );
	int y = rand_GetRangeS32( NULL, topBorder, bottomBorder );

	return hex_Flat_RectIndexToCoord( x + ( MAP_WIDTH * y ), MAP_WIDTH, MAP_HEIGHT );
}

static void generateSmallTrees( int spotX, int spotY )
{
	int numTrees = rand_GetRangeS32( NULL, MIN_SMALL_TREES, MAX_SMALL_TREES );

	int leftBorder = SPOT_TILE_MIN_X( spotX );
	int rightBorder = SPOT_TILE_MAX_X( spotX );
	int topBorder = SPOT_TILE_MIN_Y( spotY );
	int bottomBorder = SPOT_TILE_MAX_Y( spotY );

	for( int i = 0; i < numTrees; ++i ) {
		// choose a cell, then choose a tile within that cell
		int x = rand_GetRangeS32( NULL, leftBorder, rightBorder );
		int y = rand_GetRangeS32( NULL, topBorder, bottomBorder );

		int idx = x + ( y * MAP_HEIGHT );

		HexGridCoord c = hex_Flat_RectIndexToCoord( idx, MAP_WIDTH, MAP_HEIGHT );

		if( isMoveable( c ) ) {
			createSmallTreeObject( c );
		}
	}
}

static void generateLake( int spotX, int spotY )
{
	int leftBorder = SPOT_TILE_MIN_X( spotX ) + 2;
	int rightBorder = SPOT_TILE_MAX_X( spotX ) - 2;
	int topBorder = SPOT_TILE_MIN_Y( spotY ) + 2;
	int bottomBorder = SPOT_TILE_MAX_Y( spotY ) - 2;

	for( int y = topBorder; y <= bottomBorder; ++y ) {
		for( int x = leftBorder; x <= rightBorder; ++x ) {

			if( ( x == leftBorder ) || ( x == rightBorder ) || ( y == topBorder ) || ( y == bottomBorder ) ) {
				if( rand_GetNormalizedFloat( NULL ) <= LAKE_BORDER_EMPTY_CHANCE ) {
					continue;
				}
			}
			map[( y * MAP_WIDTH ) + x].type = TT_WATER;
		}
	}
}

typedef struct {
	int neighbor;
	TileType type;
} TreeConstruction;

TreeConstruction treeConstruction[] = {
	{ HD_F_NORTH, TT_TREE_N },
	{ HD_F_NORTH_EAST, TT_TREE_NE },
	{ HD_F_SOUTH_EAST, TT_TREE_SE },
	{ HD_F_SOUTH, TT_TREE_S },
	{ HD_F_SOUTH_WEST, TT_TREE_SW },
	{ HD_F_NORTH_WEST, TT_TREE_NW },
};

static void generateBigTree( int spotX, int spotY, int cellX, int cellY )
{
	// put a big tree somewhere in the cell
	int leftBorder = CELL_TILE_MIN_X( spotX, cellX );
	int rightBorder = CELL_TILE_MAX_X( spotX, cellX );
	int topBorder = CELL_TILE_MIN_Y( spotY, cellY );
	int bottomBorder = CELL_TILE_MAX_Y( spotY, cellY );

	int x = rand_GetRangeS32( NULL, leftBorder, rightBorder );
	int y = rand_GetRangeS32( NULL, topBorder, bottomBorder );

	int idx = ( y * MAP_WIDTH ) + x;

	map[idx].type = TT_TREE_CENTER;
	HexGridCoord c = hex_Flat_RectIndexToCoord( idx, MAP_WIDTH, MAP_HEIGHT );

	for( size_t i = 0; i < ARRAY_SIZE( treeConstruction ); ++i ) {
		HexGridCoord n = hex_GetNeighbor( c, treeConstruction[i].neighbor );
		if( hex_Flat_CoordInRect( n, MAP_WIDTH, MAP_HEIGHT ) ) {
			int idx = hex_Flat_CoordToRectIndex( n, MAP_WIDTH, MAP_HEIGHT );
			map[idx].type = treeConstruction[i].type;
		}
	}
}

typedef struct {
	int x;
	int y;
} Coord;
static Coord coord( int x, int y )
{
	Coord c;
	c.x = x;
	c.y = y;
	return c;
}

static void generateWoods( int spotX, int spotY )
{
	int numBigTrees = rand_GetRangeS32( NULL, MIN_BIG_TREES, MAX_BIG_TREES );
	Coord bigTreeSpots[SPOT_WIDTH * SPOT_HEIGHT];

	for( int y = 0; y < SPOT_HEIGHT; ++y ) {
		for( int x = 0; x < SPOT_WIDTH; ++x ) {
			bigTreeSpots[x + ( y * SPOT_WIDTH )] = coord( x, y );
		}
	}

	// now shuffle
	for( int i = 0; i < ( SPOT_WIDTH * SPOT_HEIGHT ) - 1; ++i ) {
		int swapIdx = rand_GetRangeS32( NULL, i, ( SPOT_WIDTH * SPOT_HEIGHT ) - 1 );
		Coord temp = bigTreeSpots[i];
		bigTreeSpots[i] = bigTreeSpots[swapIdx];
		bigTreeSpots[swapIdx] = temp;
	}

	for( int i = 0; i < numBigTrees; ++i ) {
		generateBigTree( spotX, spotY, bigTreeSpots[i].x, bigTreeSpots[i].y );
	}
}

static bool doesSBContainGridCoord( HexGridCoord* sbList, HexGridCoord* c )
{
	for( size_t i = 0; i < sb_Count( sbList ); ++i ) {
		if( compareCoords( &( sbList[i] ), c ) ) {
			return true;
		}
	}

	return false;
}

static void generateMap( )
{
	bool isValid = false;
	HexGridCoord* allCanReach = NULL;
	HexGridCoord* currentStack = NULL;
	
	while( !isValid ) {
	// set map to empty
		for( int i = 0; i < ( MAP_WIDTH * MAP_HEIGHT ); ++i ) {
			map[i].type = TT_GROUND;
			map[i].smokeLeft = 0;
			map[i].isVisible = false;
		}

		// see if there's a lake
		int lakeIdx = -1;
		if( rand_GetNormalizedFloat( NULL ) <= LAKE_CHANCE ) {
			lakeIdx = rand_GetRangeS32( NULL, 0, ( ARENA_WIDTH * ARENA_HEIGHT ) );
		}//*/

		for( int sy = 0; sy < ARENA_HEIGHT; ++sy ) {
			for( int sx = 0; sx < ARENA_WIDTH; ++sx ) {
				if( lakeIdx == ( ( sy * ARENA_WIDTH ) + sx ) ) {
					generateLake( sx, sy );
				} else {
					generateWoods( sx, sy );
				}//*/
			}
		}

		// check to see if map is valid, every ground spot should be reachable from every other ground spot
		size_t numMoveSpots = 0;
		for( int i = 0; i < ARRAY_SIZE( map ); ++i ) {
			if( isMoveableType( map[i].type ) ) {
				++numMoveSpots;
			}
		}

		HexGridCoord base;
		base.r = base.q = 0;
		while( !isMoveable( base ) ) {
			int idx = rand_GetRangeS32( NULL, 0, ARRAY_SIZE( map ) );
			base = hex_Flat_RectIndexToCoord( idx, MAP_WIDTH, MAP_HEIGHT );
		}

		// do a search, want a list of every hex that's accessible from the base hex
		sb_Clear( allCanReach );
		sb_Clear( currentStack );
		sb_Push( currentStack, base );
		size_t test = 0;
		while( sb_Count( currentStack ) > 0 ) {
			++test;
			//llog( LOG_DEBUG, "test: %i   %s", test, ( test > numMoveSpots ) ? "TOO MANY" : "good" );

			HexGridCoord curr = sb_Pop( currentStack );
			sb_Push( allCanReach, curr );

			// find all neighbors, if we haven't processed them then just push them onto the stack
			for( int i = 0; i < 6; ++i ) {
				HexGridCoord n = hex_GetNeighbor( curr, i );

				if( hex_Flat_CoordInRect( n, MAP_WIDTH, MAP_HEIGHT ) && 
					!doesTerrainBlock( n ) &&
					!doesSBContainGridCoord( allCanReach, &n ) && 
					!doesSBContainGridCoord( currentStack, &n ) ) {

					sb_Push( currentStack, n );
				}
			}
		}

		isValid = ( numMoveSpots == sb_Count( allCanReach ) );

		if( !isValid ) {
			llog( LOG_WARN, "Invalid map created, generating new one." );
		}
	}

	sb_Release( allCanReach );
	sb_Release( currentStack );

	// generate small trees
	for( int sy = 0; sy < ARENA_HEIGHT; ++sy ) {
		for( int sx = 0; sx < ARENA_WIDTH; ++sx ) {
			generateSmallTrees( sx, sy );
		}
	}
	setMapImages( );
}

static void mapVisProcess( )
{
	HexGridCoord base = sbObjects[PLAYER_OBJ_IDX].targetPos;
	for( int i = 0; i < ( MAP_WIDTH * MAP_HEIGHT ); ++i ) {
		HexGridCoord mapCoord = hex_Flat_RectIndexToCoord( i, MAP_WIDTH, MAP_HEIGHT );
		map[i].isVisible = isVisible( base, mapCoord, -1 );
	}
}

#define HEX_SIZE ( 45.0f / 2.0f )

HexGridCoord lookAtTile = { 0, 0 };
Vector2 mapOffset = { ( 170.0f + ( 630.0f / 2.0f ) ), ( 430.0f / 2.0f ) };

static Vector2 getDrawPos( HexGridCoord coord )
{
	Vector2 basePos;
	if( ( inputType == IT_LOOK ) || ( inputType == IT_AIM ) ) {
		basePos = hex_Flat_GridToPosition( HEX_SIZE, lookAtTile );
	} else {
		if( sbObjects[PLAYER_OBJ_IDX].animType == AT_MOVE ) {
			HexGridCoord startCrd = sbObjects[PLAYER_OBJ_IDX].pos;
			HexGridCoord endCrd = sbObjects[PLAYER_OBJ_IDX].targetPos;

			Vector2 startPos = hex_Flat_GridToPosition( HEX_SIZE, startCrd );
			Vector2 endPos = hex_Flat_GridToPosition( HEX_SIZE, endCrd );
			vec2_Lerp( &startPos, &endPos, ( sbObjects[PLAYER_OBJ_IDX].posT / MOVE_ANIM_TIME ), &basePos );
		} else {
			basePos = hex_Flat_GridToPosition( HEX_SIZE, sbObjects[PLAYER_OBJ_IDX].pos );
		}
	}

	vec2_Subtract( &mapOffset, &basePos, &basePos );

	Vector2 drawPos = hex_Flat_GridToPosition( HEX_SIZE, coord );
	drawPos.x = (float)( (int)drawPos.x );
	drawPos.y = (float)( (int)drawPos.y );
	vec2_Add( &drawPos, &basePos, &drawPos );

	return drawPos;
}

static bool doesTerrainBlock( HexGridCoord coord )
{
	int idx = hex_Flat_CoordToRectIndex( coord, MAP_WIDTH, MAP_HEIGHT );

	if( !isMoveableType( map[idx].type ) ) {
		return true;
	}

	return false;
}

static bool isMoveableTwo( HexGridCoord coord, size_t ignoreOne, size_t ignoreTwo )
{
	if( !hex_Flat_CoordInRect( coord, MAP_WIDTH, MAP_HEIGHT ) ) {
		return false;
	}

	if( doesTerrainBlock( coord ) ) {
		return false;
	}

	size_t found = getObjectAt( coord, true );
	if( ( found != SIZE_MAX ) && ( found != ignoreOne ) && ( found != ignoreTwo ) ) {
		return false;
	}

	return true;
}

static bool isMoveable( HexGridCoord coord )
{
	if( !hex_Flat_CoordInRect( coord, MAP_WIDTH, MAP_HEIGHT ) ) {
		return false;
	}

	if( doesTerrainBlock( coord ) ) {
		return false;
	}

	if( getObjectAt( coord, true ) != SIZE_MAX ) {
		return false;
	}

	return true;
}

static HexGridCoord* sbVisTest = NULL;
static bool isVisible( HexGridCoord lookFrom, HexGridCoord target, int32_t maxDist )
{
	if( ( lookFrom.q == target.q ) && ( lookFrom.r == target.r ) ) {
		return true;
	}

	// trace a line and see if there's anything in the way
	if( maxDist > 0 ) {
		if( hex_Distance( lookFrom, target ) > maxDist ) {
			return false;
		}
	}

	bool hitBorderAlready = false;

	sb_Clear( sbVisTest );
	hex_AllInLine( lookFrom, target, &sbVisTest );
	for( size_t i = 0; i < sb_Count( sbVisTest ); ++i ) {
		int idx = hex_Flat_CoordToRectIndex( sbVisTest[i], MAP_WIDTH, MAP_HEIGHT );

		// outside of the tree and smoke should always be visible
		if( !hitBorderAlready && ( isOuterTree( map[idx].type ) || map[idx].smokeLeft > 0 ) ) {
			hitBorderAlready = true;
			continue;
		}

		if( isBlockingType( map[idx].type ) || ( map[idx].smokeLeft > 0 ) ) {

			return false;
		}
	}

	return true;
}

static void drawMap( )
{
	HexGridCoord focus = sbObjects[PLAYER_OBJ_IDX].pos;
	if( ( inputType == IT_LOOK ) || ( inputType == IT_AIM ) ) {
		focus = lookAtTile;
	}

	Vector2 basePos = hex_Flat_GridToPosition( HEX_SIZE, focus );
	vec2_Subtract( &mapOffset, &basePos, &basePos );

	int numDrawn = 0;

	for( int i = 0; i < ( MAP_WIDTH * MAP_HEIGHT ); ++i ) {
		HexGridCoord c = hex_Flat_RectIndexToCoord( i, MAP_WIDTH, MAP_HEIGHT );
		Vector2 pos = getDrawPos( c );

		float grey = 0.0f;
		if( !map[i].isVisible ) {
			grey = 1.0f;
		}

		img_Draw_v( map[i].img, GAME_CAMERA_FLAGS, pos, pos, grey, grey, 0 );

		if( map[i].isVisible && map[i].smokeLeft > 0 ) {
			img_Draw( smokeImg, GAME_CAMERA_FLAGS, pos, pos, 0 );
		}
	}

	if( ( inputType == IT_LOOK ) || ( inputType == IT_AIM ) ) {
		Vector2 focusPos = hex_Flat_GridToPosition( HEX_SIZE, focus );
		focusPos.x = (float)( (int)focusPos.x );
		focusPos.y = (float)( (int)focusPos.y );
		vec2_Add( &focusPos, &basePos, &focusPos );
		img_Draw( tileHilite, GAME_CAMERA_FLAGS, focusPos, focusPos, 1 );
	}
}

static bool findClosestOpenSpot( HexGridCoord c, HexGridCoord* out )
{
	if( isMoveable( c ) ) {
		( *out ) = c;
		return true;
	}

	HexGridCoord* sbRing = NULL;
	bool found = false;
	int dist = 1;
	while( dist < 20 ) {
		hex_Ring( c, dist, &sbRing );

		for( size_t i = 0; i < sb_Count( sbRing ); ++i ) {
			if( hex_CoordInRect( sbRing[i], MAP_WIDTH, MAP_HEIGHT ) && isMoveable( sbRing[i] ) ) {
				( *out ) = sbRing[i];
				found = true;
				goto clean_up;
			}
		}
	}

clean_up:
	sb_Release( sbRing );
	return found;
}

//******************************************
// Message buffer
#define MAX_BUFFER_STR_LEN 64
#define MAX_BUFFER_STR_CNT 32
char messageBuffer[MAX_BUFFER_STR_CNT][MAX_BUFFER_STR_LEN];

static Color textColor;
static Color warningColor;
static Color dangerColor;
static Color nameColor;

static void drawMessageBuffer( Vector2 lowerLeft, float lineHeight, int lineCount, uint8_t depth )
{
	Vector2 basePos = lowerLeft;
	for( int i = 0; i < lineCount; ++i ) {
		txt_DisplayString( messageBuffer[i], basePos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_BASE_LINE, fontText, UI_CAMERA_FLAGS, depth, 16.0f );
		basePos.y -= lineHeight;
	}//*/
}

static void appendToTextBuffer( const char* text )
{
	memmove( &messageBuffer[1][0], &messageBuffer[0][0], sizeof( char ) * MAX_BUFFER_STR_LEN * ( MAX_BUFFER_STR_CNT - 1 ) );
	memcpy( &messageBuffer[0][0], text, MAX_BUFFER_STR_LEN );
	messageBuffer[0][MAX_BUFFER_STR_LEN - 1] = '\0';
}

static void clearMessageBuffer( void )
{
	memset( messageBuffer, 0, sizeof( messageBuffer ) );
}


//******************************************
// Input processing
static void endPlayerTurn( void )
{
	bool addTurn = lockedToMoveTurns == 0;
	isActionRunning = false;
	isPlayerActing = false;
	if( lockedToMoveTurns > 0 ) {
		--lockedToMoveTurns;
	}

	if( aimBonusTurns > 0 ) {
		--aimBonusTurns;
	}

	for( size_t i = 0; i < ARRAY_SIZE( map ); ++i ) {
		if( map[i].smokeLeft > 0 ) {
			--map[i].smokeLeft;
		}
	}

	generalObjectTurnEnd( PLAYER_OBJ_IDX, addTurn );
}

static void playerMeleeAttack( size_t targetIdx, int toHitDice, int damageDice )
{
	int targetDodge = sbObjects[targetIdx].dodgeDice;
	int targetArmor = sbObjects[targetIdx].armorDice;

	if( aimBonusTurns > 0 ) {
		toHitDice += player.sensorBonus;
	}

	if( player.sensor.health <= 0 ) {
		toHitDice -= player.sensorDamagePenalty;
	}

	char buffer[64];

	int damageSound = -1;

	if( rollDicePool( toHitDice ) >= rollDicePool( targetDodge ) ) {
		int damage = rollDicePool( damageDice ) - rollDicePool( targetArmor );
		if( damage <= 0 ) {
			SDL_snprintf( buffer, 63, "Your sword glanced off the %s.", sbObjects[targetIdx].name );
		} else {
			sbObjects[targetIdx].health -= damage;
			shakeObject( targetIdx );
			if( sbObjects[targetIdx].health <= 0 ) {
				sbObjects[targetIdx].health = 0;
				SDL_snprintf( buffer, 63, "You killed the %s.", sbObjects[targetIdx].name );
				damageSound = creatureDeadSnd;
			} else {
				SDL_snprintf( buffer, 63, "Your sword hit the %s for %i damage.", sbObjects[targetIdx].name, damage );
				damageSound = creatureDamageSnd;
			}
		}
	} else {
		SDL_snprintf( buffer, 63, "Your sword missed the %s.", sbObjects[targetIdx].name );
	}
	appendToTextBuffer( buffer );

	playSound( damageSound );
}

static void playerNormalMeleeAttack( size_t targetIdx )
{
	int toHitDice;
	int damageDice;

	if( player.melee.health <= 0 ) {
		toHitDice = player.damagedMeleeHitDice;
		damageDice = player.damagedMeleeDamageDice;
	} else {
		toHitDice = player.meleeHitDice;
		damageDice = player.meleeDamageDice;
	}

	playerMeleeAttack( targetIdx, toHitDice, damageDice );
}

static void playerMeleeOverdriveAttack( void )
{
	int toHitDice;
	int damageDice;

	if( player.melee.health <= 0 ) {
		toHitDice = player.damagedMeleeHitDice;
		damageDice = player.damagedMeleeDamageDice;
	} else {
		toHitDice = player.meleeHitDice;
		damageDice = player.meleeDamageDice;
	}

	appendToTextBuffer( "You Overdrive your sword arm, attacking everything around you." );

	shakeObject( PLAYER_OBJ_IDX );

	HexGridCoord base = sbObjects[PLAYER_OBJ_IDX].pos;
	for( int i = 0; i < 6; ++i ) {
		HexGridCoord n = hex_GetNeighbor( base, i );
		size_t target = getObjectAt( n, true );
		if( target != SIZE_MAX ) {
			playerMeleeAttack( target, toHitDice, damageDice );
		}
	}

	// check for overdrive damage
	if( rand_GetNormalizedFloat( NULL ) <= player.melee.chanceOfDamage ) {
		appendToTextBuffer( "You've damaged your sword arm." );
		player.melee.health -= 1;
		if( player.melee.health <= 0 ) {
			playSound( playerBreakSnd );
		}
	}
}

static void playerRangedAttack( size_t targetIdx, int toHitDice, int damageDice )
{
	int targetDodge = sbObjects[targetIdx].dodgeDice;
	int targetArmor = sbObjects[targetIdx].armorDice;

	if( aimBonusTurns > 0 ) {
		toHitDice += player.sensorBonus;
	}

	if( player.sensor.health <= 0 ) {
		toHitDice -= player.sensorDamagePenalty;
	}

	char buffer[64];

	playSound( playerShootSnd );

	int hitSound = -1;

	if( rollDicePool( toHitDice ) >= rollDicePool( targetDodge ) ) {
		int damage = rollDicePool( damageDice ) - rollDicePool( targetArmor );
		if( damage <= 0 ) {
			SDL_snprintf( buffer, 63, "Your shot glanced off the %s.", sbObjects[targetIdx].name );
		} else {
			sbObjects[targetIdx].health -= damage;
			if( sbObjects[targetIdx].health <= 0 ) {
				sbObjects[targetIdx].health = 0;
				SDL_snprintf( buffer, 63, "You killed the %s.", sbObjects[targetIdx].name );
				hitSound = creatureDeadSnd;
			} else {
				SDL_snprintf( buffer, 63, "Your shot hits the %s for %i damage.", sbObjects[targetIdx].name, damage );
				hitSound = creatureDamageSnd;
			}
		}
	} else {
		SDL_snprintf( buffer, 63, "Your shot goes wide, missing the %s.", sbObjects[targetIdx].name );
	}
	appendToTextBuffer( buffer );

	createProjectileObject( sbObjects[PLAYER_OBJ_IDX].pos, sbObjects[targetIdx].pos, targetIdx, hitSound );
}

static void playerNormalRangedAttack( size_t targetIdx )
{
	int toHitDice = player.rangedHitDice;
	int damageDice = player.rangedDamageDice;

	playerRangedAttack( targetIdx, toHitDice, damageDice );
}

static void playerOverdriveRangedAttack( size_t targetIdx )
{
	int toHitDice = player.boostedRangegdHitDice;
	int damageDice = player.boostedRangedDamageDice;

	appendToTextBuffer( "You overdrive you gun and shoot." );

	playerRangedAttack( targetIdx, toHitDice, damageDice );

	if( rand_GetNormalizedFloat( NULL ) <= player.ranged.chanceOfDamage ) {
		appendToTextBuffer( "You damaged your gun." );
		player.ranged.health -= 1;
		if( player.ranged.health <= 0 ) {
			playSound( playerBreakSnd );
		}
	}
}

static void moveLookInput( SDL_Keycode symbol, int maxRange, bool forceVisible )
{
	HexGridCoord newCoord = lookAtTile;

	switch( symbol ) {
	case SDLK_w:
		newCoord = hex_GetNeighbor( lookAtTile, HD_F_NORTH );
		break;
	case SDLK_e:
		newCoord = hex_GetNeighbor( lookAtTile, HD_F_NORTH_EAST );
		break;
	case SDLK_d:
		newCoord = hex_GetNeighbor( lookAtTile, HD_F_SOUTH_EAST );
		break;
	case SDLK_s:
		newCoord = hex_GetNeighbor( lookAtTile, HD_F_SOUTH );
		break;
	case SDLK_a:
		newCoord = hex_GetNeighbor( lookAtTile, HD_F_SOUTH_WEST );
		break;
	case SDLK_q:
		newCoord = hex_GetNeighbor( lookAtTile, HD_F_NORTH_WEST );
		break;
	}

	// can't go beyond a certain range
	if( ( maxRange > 0 ) && ( hex_Distance( sbObjects[PLAYER_OBJ_IDX].pos, newCoord ) > maxRange ) ) {
		playSound( invalidSnd );
		appendToTextBuffer( "Outside valid range." );
		newCoord = lookAtTile;
	}

	if( forceVisible && !map[hex_Flat_CoordToRectIndex( newCoord, MAP_WIDTH, MAP_HEIGHT )].isVisible ) {
		playSound( invalidSnd );
		appendToTextBuffer( "Outside your view range." );
		newCoord = lookAtTile;
	}

	if( hex_Flat_CoordInRect( newCoord, MAP_WIDTH, MAP_HEIGHT ) ) {
		lookAtTile = newCoord;
	}
}

static void processNormalInput( SDL_Keycode symbol )
{
	HexGridCoord newCoord = sbObjects[PLAYER_OBJ_IDX].pos;
	bool moved = false;

	switch( symbol ) {
	case SDLK_w:
		moved = true;
		newCoord = hex_GetNeighbor( newCoord, HD_F_NORTH );
		break;
	case SDLK_e:
		moved = true;
		newCoord = hex_GetNeighbor( newCoord, HD_F_NORTH_EAST );
		break;
	case SDLK_d:
		moved = true;
		newCoord = hex_GetNeighbor( newCoord, HD_F_SOUTH_EAST );
		break;
	case SDLK_s:
		moved = true;
		newCoord = hex_GetNeighbor( newCoord, HD_F_SOUTH );
		break;
	case SDLK_a:
		moved = true;
		newCoord = hex_GetNeighbor( newCoord, HD_F_SOUTH_WEST );
		break;
	case SDLK_q:
		moved = true;
		newCoord = hex_GetNeighbor( newCoord, HD_F_NORTH_WEST );
		break;
	case SDLK_o:
		if( lockedToMoveTurns <= 0 ) {
			inputType = IT_OVERDRIVE;
		}
		break;
	case SDLK_i:
		if( lockedToMoveTurns <= 0 ) {
			if( player.ranged.health > 0 ) {
				lookAtTile = newCoord;;
				inputType = IT_AIM;
			} else {
				appendToTextBuffer( "Your gun is damaged and cannot shoot." );
			}
		}
		break;
	case SDLK_l:
		if( player.sensor.health > 0 ) {
			lookAtTile = newCoord;;
			inputType = IT_LOOK;
		} else {
			appendToTextBuffer( "Your sensors are damaged and you cannot scan." );
		}
		break;
	case SDLK_PERIOD:
		endPlayerTurn( );
		// skip turn
		break;
	case SDLK_h:
		// show help
		drawHelp = true;
		inputType = IT_HELP;
		break;
	}

	if( moved && hex_Flat_CoordInRect( newCoord, MAP_WIDTH, MAP_HEIGHT ) ) {
		// if there's an object in the new tile attack it
		size_t foundObj = getObjectAt( newCoord, true );

		if( foundObj == SIZE_MAX ) {
			// nothing alive found, move if valid
			if( !doesTerrainBlock( newCoord ) ) {
				moveObjecTTo( PLAYER_OBJ_IDX, newCoord );
				mapVisProcess( );
				endPlayerTurn( );
			}
		} else {
			if( lockedToMoveTurns <= 0 ) {
				// melee attack
				meleeAttackWithObj( PLAYER_OBJ_IDX, newCoord );
				playerNormalMeleeAttack( foundObj );

				endPlayerTurn( );
			} else {
				appendToTextBuffer( "Cannot attack while overdriving legs." );
			}
		}
	}
}

static void processOverdriveInput( SDL_Keycode symbol )
{
	switch( symbol ) {
	case SDLK_1:
		if( player.ranged.health <= 0 ) {
			playSound( invalidSnd );
			return;
		}
		useOverdriveShot = true;
		lookAtTile = sbObjects[PLAYER_OBJ_IDX].pos;
		inputType = IT_AIM;
		break;
	case SDLK_2:
		if( player.melee.health <= 0 ) {
			playSound( invalidSnd );
			return;
		}
		playerMeleeOverdriveAttack( );
		endPlayerTurn( );
		break;
	case SDLK_3:
		if( player.body.health <= 0 ) {
			playSound( invalidSnd );
			return;
		}
		appendToTextBuffer( "You overdrive your torso, giving you a temporary shield." );
		if( rand_GetNormalizedFloat( NULL ) <= player.body.chanceOfDamage ) {
			appendToTextBuffer( "You've damaged your torso." );
			player.body.health -= 1;
			if( player.body.health <= 0 ) {
				playSound( playerBreakSnd );
			}
		}
		endPlayerTurn( );
		sbObjects[PLAYER_OBJ_IDX].immunityTurns = 2;
		break;
	case SDLK_4:
		if( player.legs.health <= 0 ) {
			playSound( invalidSnd );
			return;
		}
		aq_PushToStartOfQueue( PLAYER_OBJ_IDX );
		aq_PushToStartOfQueue( PLAYER_OBJ_IDX );
		appendToTextBuffer( "You overdrive your legs, giving you more mobility." );
		if( rand_GetNormalizedFloat( NULL ) <= player.legs.chanceOfDamage ) {
			appendToTextBuffer( "You've damaged your legs." );
			player.legs.health -= 1;
			if( player.legs.health <= 0 ) {
				playSound( playerBreakSnd );
			}
		}
		shakeObject( PLAYER_OBJ_IDX );
		endPlayerTurn( );
		lockedToMoveTurns = 2;
		break;
	case SDLK_5:
		if( player.sensor.health <= 0 ) {
			playSound( invalidSnd );
			return;
		}
		appendToTextBuffer( "You overdrive your sensors, giving you better aim for a while." );
		if( rand_GetNormalizedFloat( NULL ) <= player.sensor.chanceOfDamage ) {
			appendToTextBuffer( "You've damaged your sensors." );
			player.sensor.health -= 1;
			if( player.sensor.health <= 0 ) {
				playSound( playerBreakSnd );
			}
		}
		shakeObject( PLAYER_OBJ_IDX );
		endPlayerTurn( );
		aimBonusTurns = player.sensorDuration;
		break;
	case SDLK_6:
		if( player.smoke.health <= 0 ) {
			playSound( invalidSnd );
			return;
		}
		map[hex_Flat_CoordToRectIndex( sbObjects[PLAYER_OBJ_IDX].pos, MAP_WIDTH, MAP_HEIGHT )].smokeLeft = player.smokeDuration;
		for( int i = 0; i < 6; ++i ) {
			HexGridCoord n = hex_GetNeighbor( sbObjects[PLAYER_OBJ_IDX].pos, i );
			map[hex_Flat_CoordToRectIndex( n, MAP_WIDTH, MAP_HEIGHT )].smokeLeft = player.smokeDuration;
		}
		appendToTextBuffer( "You overdrive your smoke screen, giving you some cover." );
		if( rand_GetNormalizedFloat( NULL ) <= player.smoke.chanceOfDamage ) {
			appendToTextBuffer( "You've damaged your smoke screen." );
			player.smoke.health -= 1;
			if( player.smoke.health <= 0 ) {
				playSound( playerBreakSnd );
			}
		}
		mapVisProcess( );
		shakeObject( PLAYER_OBJ_IDX );
		inputType = IT_NORMAL;
		break;
	case SDLK_PERIOD:
		inputType = IT_NORMAL;
		break;
	}
}

static void processLookInput( SDL_Keycode symbol )
{
	moveLookInput( symbol, -1, false );
	switch( symbol ) {
	case SDLK_PERIOD:
		inputType = IT_NORMAL;
		break;
	}
}

static void processAimInput( SDL_Keycode symbol )
{
	size_t targetObj = SIZE_MAX;

	moveLookInput( symbol, 10, true );
	switch( symbol ) {
	case SDLK_PERIOD:
		if( useOverdriveShot ) {
			inputType = IT_OVERDRIVE;
		} else {
			inputType = IT_NORMAL;
		}
		useOverdriveShot = false;
		break;
	case SDLK_k:
		// attack
		targetObj = getObjectAt( lookAtTile, true );

		if( targetObj != SIZE_MAX ) {

			if( useOverdriveShot ) {
				playerOverdriveRangedAttack( targetObj );
			} else {
				playerNormalRangedAttack( targetObj );
			}
			endPlayerTurn( );
		}

		break;
	}
}

static void processWonInput( SDL_Keycode symbol )
{

}

static void processLostInput( SDL_Keycode symbol )
{
	inputType = IT_INTRO;
}

static void processHelpInput( SDL_Keycode symbol )
{
	inputType = IT_NORMAL;
	drawHelp = false;
}

static void processRepairInput( SDL_Keycode symbol )
{
	Part* part = NULL;
	switch( symbol ) {
	case SDLK_1:
		part = &player.ranged;
		break;
	case SDLK_2:
		part = &player.melee;
		break;
	case SDLK_3:
		part = &player.body;
		break;
	case SDLK_4:
		part = &player.legs;
		break;
	case SDLK_5:
		part = &player.sensor;
		break;
	case SDLK_6:
		part = &player.smoke;
		break;
	case SDLK_PERIOD:
		++level;
		createLevel( );
		inputType = IT_NORMAL;
		break;
	}

	if( part != NULL ) {
		if( repairPointsLeft > 0 ) {
			if( part->health < part->maxHealth ) {
				++part->health;
				char buffer[64];
				if( part->health < part->maxHealth ) {
					SDL_snprintf( buffer, 63, "You perform some repairs on the %s.", part->name );
				} else {
					SDL_snprintf( buffer, 63, "You've fully repaired the %s.", part->name );
				}
				appendToTextBuffer( buffer );
				--repairPointsLeft;
				playSound( playerRepairSnd );
			} else {
				playSound( invalidSnd );
			}
		} else {
			playSound( invalidSnd );
		}
	}
}

static void processIntroInput( SDL_Keycode symbol )
{
	level = 0;
	initPlayer( );
	createLevel( );
	inputType = IT_NORMAL;
	drawIntro = false;
}

static void processInput( SDL_Event* evt )
{
	if( !isPlayerActing ) return;

	if( evt->type != SDL_KEYDOWN ) {
		return;
	}

	if( evt->key.repeat ) {
		return;
	}

	switch( inputType ) {
	case IT_NORMAL:
		processNormalInput( evt->key.keysym.sym );
		break;
	case IT_OVERDRIVE:
		processOverdriveInput( evt->key.keysym.sym );
		break;
	case IT_LOOK:
		processLookInput( evt->key.keysym.sym );
		break;
	case IT_AIM:
		processAimInput( evt->key.keysym.sym );
		break;
	case IT_WON:
		processWonInput( evt->key.keysym.sym );
		break;
	case IT_LOST:
		processLostInput( evt->key.keysym.sym );
		break;
	case IT_HELP:
		processHelpInput( evt->key.keysym.sym );
		break;
	case IT_INTRO:
		processIntroInput( evt->key.keysym.sym );
		break;
	case IT_REPAIR:
		processRepairInput( evt->key.keysym.sym );
		break;
	}
}

typedef struct {
	int cost;
	int levelRequirement;
	void( *create )( HexGridCoord pos );
	bool ignoreFirst;
} EnemyChoice;

static EnemyChoice enemyChoices[] = {
	{ 1, 0, createWeakEnemy, false },
	{ 2, 0, createStrongEnemy, false },
	{ 2, 1, createAlarmEnemy, true },
	{ 4, 2, createArmoredEnemy, false },
	{ 4, 2, createRangedEnemy, false },
	{ 4, 4, createExplosiveEnemy, false }
};

static int getPoints( void )
{
	if( level > 4 ) {
		return 5 + ( level * 4 );
	} else {
		return 5 + ( level * 2 );
	}
}

static void createLevel( void )
{
	clearObjects( );

	generateMap( );

	// choose the player start spot
	int spawnIdx;
	HexGridCoord spawnPos;
	do {
		spawnIdx = rand_GetRangeS32( NULL, 0, ARRAY_SIZE( map ) - 1 );
		spawnPos = hex_Flat_RectIndexToCoord( spawnIdx, MAP_WIDTH, MAP_HEIGHT );
	} while( !isMoveable( spawnPos ) );
	createPlayerObject( spawnPos );

	// create all the enemies for the levels
	int points = getPoints( );
	bool firstChoice = true;
	int* sbValidChoices = NULL;
	while( points > 0 ) {
		sb_Clear( sbValidChoices );
		for( size_t i = 0; i < ARRAY_SIZE( enemyChoices ); ++i ) {
			if( enemyChoices[i].levelRequirement > level ) continue;
			if( enemyChoices[i].cost > points ) continue;
			if( firstChoice && enemyChoices[i].ignoreFirst ) continue;

			sb_Push( sbValidChoices, i );
		}

		int choice = sbValidChoices[rand_GetRangeS32( NULL, 0, sb_Count( sbValidChoices ) - 1 )];
		points -= enemyChoices[choice].cost;

		do{
			spawnIdx = rand_GetRangeS32( NULL, 0, ARRAY_SIZE( map ) - 1 );
			spawnPos = hex_Flat_RectIndexToCoord( spawnIdx, MAP_WIDTH, MAP_HEIGHT );
		} while( !isMoveable( spawnPos ) );

		enemyChoices[choice].create( spawnPos );

		firstChoice = false;
	}
	sb_Release( sbValidChoices );

	isPlayerActing = false;
	isActionRunning = false;

	aq_Clear( );
	// add all objects with an action to the queue
	for( size_t i = 0; i < sb_Count( sbObjects ); ++i ) {
		if( sbObjects[i].action != NULL ) {
			aq_PushToEndOfQueue( i );
		}
	}

	mapVisProcess( );
}

//******************************************
// State machine
static int arenaScreen_Enter( void )
{
	cam_TurnOnFlags( UI_CAMERA, UI_CAMERA_FLAGS );
	cam_TurnOnFlags( GAME_CAMERA, GAME_CAMERA_FLAGS );

	gfx_SetClearColor( CLR_BLACK );

	inputType = IT_INTRO;
	drawIntro = true;

	textColor = clr_byte( 0, 184, 0, 255 );
	nameColor = clr_byte( 60, 188, 252, 255 );
	warningColor = clr_byte( 248, 184, 0, 255 );
	dangerColor = clr_byte( 248, 56, 0, 255 );

	clearMessageBuffer( );

	initPlayer( );

	drawHelp = false;

	createLevel( );

	snd_PlayStreaming( music, 0.5f, 0.0f );

	return 1;
}

static int arenaScreen_Exit( void )
{
	return 1;
}

static void arenaScreen_ProcessEvents( SDL_Event* e )
{
	processInput( e );
}

static void arenaScreen_Process( void )
{}

static void drawUIBox( Vector2 pos, Vector2 size, uint8_t depth )
{
	img_Draw3x3v( sbBoxBorder, UI_CAMERA_FLAGS, pos, pos, size, size, depth );
}

static void drawPartStatus( Part part, Vector2* pos )
{
	char buffer[32];

	Color clr;
	if( part.health == 0 ) {
		clr = dangerColor;
	} else if( part.health <= ( part.maxHealth / 2 ) ) {
		clr = warningColor;
	} else {
		clr = textColor;
	}

	SDL_snprintf( buffer, 31, "  %s: %i/%i", part.name, part.health, part.maxHealth );
	txt_DisplayString( buffer, *pos, clr, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	pos->y += 16.0f;

	if( part.health <= 0 ) {
		txt_DisplayString( part.shortDamagedDesc, *pos, clr, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
		pos->y += 16.0f;
	}
	
}

static void drawOurStatus( Vector2 basePos )
{
	txt_DisplayString( "Mech Status", basePos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	basePos.y += 16.0f;

	if( sbObjects[PLAYER_OBJ_IDX].immunityTurns > 0 ) {
		txt_DisplayString( "SHIELD ACTIVE!", basePos, warningColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
		basePos.y += 16.0f;
	}

	if( aimBonusTurns > 0 ) {
		txt_DisplayString( "AIM BONUS ACTIVE!", basePos, warningColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
		basePos.y += 16.0f;
	}

	drawPartStatus( player.ranged, &basePos );
	drawPartStatus( player.melee, &basePos );
	drawPartStatus( player.body, &basePos );
	drawPartStatus( player.legs, &basePos );
	drawPartStatus( player.sensor, &basePos );
	drawPartStatus( player.smoke, &basePos );
}

static void drawTheirStatus( Vector2 basePos )
{
	int mapIdx = hex_Flat_CoordToRectIndex( lookAtTile, MAP_WIDTH, MAP_HEIGHT );
	if( !map[mapIdx].isVisible ) {
		txt_DisplayString( "Hidden area", basePos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
		return;
	}

	size_t target = getObjectAt( lookAtTile, false );
	if( target == SIZE_MAX ) {
		return;
	}

	if( target == PLAYER_OBJ_IDX ) {
		drawOurStatus( basePos );
		return;
	}//*/

	Object* obj = &( sbObjects[target] );

	Color clr;
	if( obj->health < ( obj->maxHealth / 4 ) ) {
		clr = dangerColor;
	} else if( obj->health < ( obj->maxHealth / 2 ) ) {
		clr = warningColor;
	} else {
		clr = textColor;
	}

	char buffer[32];
	txt_DisplayString( "Scanned Object", basePos, clr, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	basePos.y += 16.0f;

	// name
	txt_DisplayString( obj->name, basePos, clr, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	basePos.y += 16.0f;

	switch( obj->state ) {
	case OS_IDLE:
		txt_DisplayString( "Idle", basePos, clr, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
		basePos.y += 16.0f;
		break;
	case OS_CURIOUS:
		txt_DisplayString( "Curious", basePos, clr, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
		basePos.y += 16.0f;
		break;
	case OS_AGGRESIVE:
		txt_DisplayString( "Aggresive", basePos, clr, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
		basePos.y += 16.0f;
		break;
	}

	if( obj->immunityTurns > 0 ) {
		txt_DisplayString( "SHIELD ACTIVE!", basePos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
		basePos.y += 16.0f;
	}

	//  health
	if( obj->health == 0 ) {
		txt_DisplayString( "Dead", basePos, clr, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	} else {
		SDL_snprintf( buffer, 31, "Health: %i/%i", obj->health, obj->maxHealth );
		txt_DisplayString( buffer, basePos, clr, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	}
	
	basePos.y += 16.0f;

	//  description
	txt_DisplayString( obj->desc, basePos, clr, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
}

static void drawStatus( void )
{
	// if we're over another creature then draw their status
	//  otherwise draw ours

	if( ( inputType == IT_LOOK ) || ( inputType == IT_AIM ) ) {
		// draw whatever we're over
		drawTheirStatus( vec2( 14.0f, 12.0f ) );
	} else {
		drawOurStatus( vec2( 14.0f, 12.0f ) );
	}
}

static void drawNormalInputs( Vector2 startPos )
{
	txt_DisplayString( "Standard Input", startPos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	startPos.y += 16.0f;

	txt_DisplayString( "Move/Melee:", startPos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	startPos.y += 16.0f;

	txt_DisplayString( "  w - North", startPos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	startPos.y += 16.0f;

	txt_DisplayString( "  e - North-East", startPos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	startPos.y += 16.0f;

	txt_DisplayString( "  d - South-East", startPos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	startPos.y += 16.0f;

	txt_DisplayString( "  s - South", startPos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	startPos.y += 16.0f;

	txt_DisplayString( "  a - South-West", startPos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	startPos.y += 16.0f;

	txt_DisplayString( "  q - North-West", startPos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	startPos.y += 16.0f;

	if( lockedToMoveTurns <= 0 ) {
		txt_DisplayString( "o - Overdrive part", startPos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
		startPos.y += 16.0f;

		if( player.ranged.health == 0 ) {
			txt_DisplayString( "Shooting disabled", startPos, dangerColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
		} else {
			txt_DisplayString( "i - Aim shot", startPos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
		}
		startPos.y += 16.0f;
	}

	if( player.sensor.health == 0 ) {
		txt_DisplayString( "Look disabled", startPos, dangerColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	} else {
		txt_DisplayString( "l - Look", startPos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	}
	startPos.y += 16.0f;

	txt_DisplayString( ". - Skip Turn", startPos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	startPos.y += 16.0f;

	txt_DisplayString( "h - Help", startPos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	startPos.y += 16.0f;
}

static void drawOverdriveInput_Part( char input, Part p, Vector2* pos )
{
	char choiceText[32];
	char descText[32];
	Color clr;

	SDL_snprintf( choiceText, 31, "%c - %s", input, p.name );
	if( p.health == 0 ) {
		SDL_snprintf( descText, 31, "  Cannot use, damaged" );
		clr = dangerColor;
	} else {
		if( p.health <= ( p.maxHealth / 2 ) ) {
			clr = warningColor;
		} else {
			clr = textColor;
		}

		SDL_snprintf( descText, 31, "  %s", p.shortOverdriveDesc );
	}

	txt_DisplayString( choiceText, *pos, clr, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	pos->y += 16.0f;
	txt_DisplayString( descText, *pos, clr, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	pos->y += 16.0f;
}

static void drawOverdriveInputs( Vector2 startPos )
{
	txt_DisplayString( "Overdrive Choice", startPos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	startPos.y += 16.0f;

	txt_DisplayString( "Choose Part:", startPos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	startPos.y += 16.0f;

	drawOverdriveInput_Part( '1', player.ranged, &startPos );
	drawOverdriveInput_Part( '2', player.melee, &startPos );
	drawOverdriveInput_Part( '3', player.body, &startPos );
	drawOverdriveInput_Part( '4', player.legs, &startPos );
	drawOverdriveInput_Part( '5', player.sensor, &startPos );
	drawOverdriveInput_Part( '6', player.smoke, &startPos );

	txt_DisplayString( ". - Cancel", startPos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	startPos.y += 16.0f;
}

static void drawLookInputs( Vector2 startPos )
{
	txt_DisplayString( "Looking", startPos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	startPos.y += 16.0f;

	txt_DisplayString( "Move Cursor:", startPos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	startPos.y += 16.0f;

	txt_DisplayString( "  w - North", startPos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	startPos.y += 16.0f;

	txt_DisplayString( "  e - North-East", startPos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	startPos.y += 16.0f;

	txt_DisplayString( "  d - South-East", startPos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	startPos.y += 16.0f;

	txt_DisplayString( "  s - South", startPos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	startPos.y += 16.0f;

	txt_DisplayString( "  a - South-West", startPos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	startPos.y += 16.0f;

	txt_DisplayString( "  q - North-West", startPos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	startPos.y += 16.0f;

	txt_DisplayString( ". - Cancel", startPos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	startPos.y += 16.0f;
}

static void drawAimInputs( Vector2 startPos )
{
	if( useOverdriveShot ) {
		txt_DisplayString( "Aiming Overdrive Shot", startPos, warningColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	} else {
		txt_DisplayString( "Aiming Shot", startPos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	}
	startPos.y += 16.0f;

	txt_DisplayString( "Move Cursor:", startPos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	startPos.y += 16.0f;

	txt_DisplayString( "  w - North", startPos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	startPos.y += 16.0f;

	txt_DisplayString( "  e - North-East", startPos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	startPos.y += 16.0f;

	txt_DisplayString( "  d - South-East", startPos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	startPos.y += 16.0f;

	txt_DisplayString( "  s - South", startPos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	startPos.y += 16.0f;

	txt_DisplayString( "  a - South-West", startPos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	startPos.y += 16.0f;

	txt_DisplayString( "  q - North-West", startPos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	startPos.y += 16.0f;

	txt_DisplayString( "k - Attack", startPos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	startPos.y += 16.0f;

	txt_DisplayString( ". - Cancel", startPos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	startPos.y += 16.0f;
}

static void drawWinInputs( Vector2 startPos )
{
	txt_DisplayString( "You've survived.\nPress any key\nto continue.", startPos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
}

static void drawLossInputs( Vector2 startPos )
{
	txt_DisplayString( "You've died.\nPress any key\nto continue.", startPos, dangerColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
}

static void drawDebugText( Vector2 startPos )
{
	char focusPos[32];
	SDL_snprintf( focusPos, 31, "Focus: %i, %i", lookAtTile.q, lookAtTile.r );
	txt_DisplayString( focusPos, startPos, CLR_MAGENTA, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	startPos.y += 16.0f;

	SDL_snprintf( focusPos, 31, "Player: %i, %i", sbObjects[0].pos.q, sbObjects[0].pos.r );
	txt_DisplayString( focusPos, startPos, CLR_MAGENTA, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	startPos.y += 16.0f;
}

static void drawRepairInputs( Vector2 startPos )
{
	txt_DisplayString( "Choose Part To repair", startPos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	startPos.y += 16.0f;

	txt_DisplayString( "Choose Part:", startPos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
	startPos.y += 16.0f;

	drawOverdriveInput_Part( '1', player.ranged, &startPos );
	drawOverdriveInput_Part( '2', player.melee, &startPos );
	drawOverdriveInput_Part( '3', player.body, &startPos );
	drawOverdriveInput_Part( '4', player.legs, &startPos );
	drawOverdriveInput_Part( '5', player.sensor, &startPos );
	drawOverdriveInput_Part( '6', player.smoke, &startPos );

	txt_DisplayString( ". - Finish", startPos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
}

static void drawIntroInfo( )
{
	Vector2 ul = vec2( 170.0f, 0.0f );
	Vector2 size = vec2( 630.0f, 430.0f );
	Vector2 center;
	vec2_AddScaled( &ul, &size, 0.5f, &center );
	drawUIBox( center, size, 10 );

	ul.x += 12;
	ul.y += 12;
	size.w -= 24;
	size.h -= 24;
	txt_DisplayTextArea( (uint8_t*)"After the battle deep in the forests of the planet Xahil you find yourself separated from the rest of your platoon. You will have to survive while trying to contact your allies.\n\nValid inputs will appear in the bottom left panel. Your status, or the status of what you're currently targeting will be in the upper right panel. Messages will appear in the bottom panel.",
		ul, size, textColor, HORIZ_ALIGN_CENTER, VERT_ALIGN_CENTER, fontText, 0, NULL, UI_CAMERA_FLAGS, 11, 32.0f );
}

static void drawRepairInfo( )
{
	Vector2 ul = vec2( 170.0f, 0.0f );
	Vector2 size = vec2( 630.0f, 430.0f );
	Vector2 center;
	vec2_AddScaled( &ul, &size, 0.5f, &center );
	drawUIBox( center, size, 10 );

	ul.x += 12;
	ul.y += 12;
	size.w -= 24;
	size.h -= 24;
	txt_DisplayTextArea( (uint8_t*)"You've survived a day. With night approaching you eat some rations and start some repairs on your mech.\n\nChoose what you want to repair.",
		ul, size, textColor, HORIZ_ALIGN_CENTER, VERT_ALIGN_CENTER, fontText, 0, NULL, UI_CAMERA_FLAGS, 11, 32.0f );

	char buffer[64];
	Color clr;
	SDL_snprintf( buffer, 63, "You have %i repair points left", repairPointsLeft );
	if( repairPointsLeft > 0 ) {
		clr = textColor;
	} else {
		clr = dangerColor;
	}
	txt_DisplayTextArea( (uint8_t*)buffer,
		ul, size, textColor, HORIZ_ALIGN_CENTER, VERT_ALIGN_BOTTOM, fontText, 0, NULL, UI_CAMERA_FLAGS, 11, 24.0f );
}

static void drawInputs( )
{
	Vector2 basePos = vec2( 14.0f, 310.0f );

	if( !isPlayerActing ) {
		txt_DisplayString( "Waiting...", basePos, CLR_MAGENTA, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
		return;
	}

	switch( inputType ) {
	case IT_NORMAL:
		drawNormalInputs( basePos );
		break;
	case IT_OVERDRIVE:
		drawOverdriveInputs( basePos );
		break;
	case IT_LOOK:
		drawLookInputs( basePos );
		break;
	case IT_AIM:
		drawAimInputs( basePos );
		break;
	case IT_WON:
		drawWinInputs( basePos );
		break;
	case IT_LOST:
		drawLossInputs( basePos );
		break;
	case IT_HELP:
		// help will have the keys to use
		break;
	case IT_INTRO:
		drawIntroInfo( );
		txt_DisplayString( "Press any key\nto start.", basePos, textColor, HORIZ_ALIGN_LEFT, VERT_ALIGN_TOP, fontText, UI_CAMERA_FLAGS, 1, 16.0f );
		break;
	case IT_REPAIR:
		drawRepairInputs( basePos );
		drawRepairInfo( );
		break;
	default:
		assert( "Invalid input type." );
	}
}

static void arenaScreen_Draw( void )
{
	if( drawHelp ) {
		img_Draw( helpImg, UI_CAMERA_FLAGS, vec2( 400.0f, 300.0f ), vec2( 400.0f, 300.0f ), 0 );
		return;
	}

	// draw status
	drawUIBox( vec2( 170.0f / 2.0f, 300.0f / 2.0f ), vec2( 170.0f, 300.0f ), 0 );
	drawStatus( );

	// draw valid inputs
	drawUIBox( vec2( 170.0f / 2.0f, 300.0f + ( 300.0f / 2.0f ) ), vec2( 170.0f, 300.0f ), 0 );
	drawInputs( );

	// draw message buffer
	drawUIBox( vec2( ( 170.0f + ( 630.0f / 2.0f ) ), ( 430.0f + ( 170.0f / 2.0f ) ) ), vec2( 630.0f, 170.0f ), 0 );
	drawMessageBuffer( vec2( 187.0f, 582.0f ), 16.0f, 9, 1 );//*/

	drawMap( );
	drawObjects( );

	//drawDebugText( vec2( 400.0f, 0.0f ) );
}

static void arenaScreen_PhysicsTick( float dt )
{
	if( soundTimeOut > 0.0f ) {
		soundTimeOut -= dt;
	}

	if( animateObjects( dt ) ) {
		if( !isActionRunning ) {

			checkForDeath( );

			// all done, start the next action
			size_t actor = aq_GetNextObjIdx( );
			//llog( LOG_DEBUG, "No actions running, next %i", actor );
			assert( actor != SIZE_MAX );

			sbObjects[actor].action( actor );
		}
	}
}

GameState arenaScreenState = { arenaScreen_Enter, arenaScreen_Exit, arenaScreen_ProcessEvents,
	arenaScreen_Process, arenaScreen_Draw, arenaScreen_PhysicsTick };

static void startPlayerAction( size_t idx )
{
	// see if all enemies are dead
	bool isEverythingInTheTrunkDead = true;
	for( size_t i = 0; ( i < sb_Count( sbObjects ) ) && isEverythingInTheTrunkDead; ++i ) {
		if( sbObjects[i].needsToDie && ( sbObjects[i].state != OS_DEAD ) ) {
			isEverythingInTheTrunkDead = false;
		}
	}

	if( isEverythingInTheTrunkDead ) {
		inputType = IT_REPAIR;
		repairPointsLeft = 8;
	}

	useOverdriveShot = false;
	if( ( inputType != IT_LOST ) && ( inputType != IT_REPAIR ) && ( inputType != IT_INTRO ) ) inputType = IT_NORMAL;
	isActionRunning = true;
	isPlayerActing = true;
}

static void checkForPlayer( size_t idx )
{
	if( isVisible( sbObjects[idx].pos, sbObjects[PLAYER_OBJ_IDX].pos, sbObjects[idx].sightRange ) ) {
		sbObjects[PLAYER_OBJ_IDX].state = OS_AGGRESIVE;
	}
}

static void aiEnterIdle( size_t idx )
{
	sbObjects[idx].state = OS_CURIOUS;
	sbObjects[idx].counter = rand_GetRangeS32( NULL, 3, 5 );
}

static void aiEnterCurious( size_t idx, HexGridCoord target )
{
	sbObjects[idx].state = OS_CURIOUS;
	sbObjects[idx].counter = 20;
	sbObjects[idx].curiousSpot = target;
}

static void aiEnterAggresive( size_t idx )
{
	sbObjects[idx].state = OS_AGGRESIVE;
}

static void idleAISense( size_t idx )
{
	// if we can see the player
	if( isVisible( sbObjects[idx].pos, sbObjects[PLAYER_OBJ_IDX].pos, sbObjects[idx].sightRange ) ) {
		//llog( LOG_DEBUG, "Sees player!" );
		aiEnterAggresive( idx );
		return;
	}

	//llog( LOG_DEBUG, "staying idle" );
}

static void curiousAISense( size_t idx )
{
	// if we can see the player
	if( isVisible( sbObjects[idx].pos, sbObjects[PLAYER_OBJ_IDX].pos, sbObjects[idx].sightRange ) ) {
		aiEnterAggresive( idx );
		//llog( LOG_DEBUG, "Sees player!" );
		return;
	}

	// if we're at the curious spot, or it's been too many turns then switch back to idle
	--sbObjects[idx].counter;
	if( compareCoords( &sbObjects[idx].pos, &sbObjects[idx].curiousSpot ) ) {
		aiEnterIdle( idx );
		//llog( LOG_DEBUG, "can't find spot" );
		return;
	}

	//llog( LOG_DEBUG, "staying curious" );
}

static void aggresiveAISense( size_t idx )
{
	// if we can't see the player move to the last spot we saw them
	if( !isVisible( sbObjects[idx].pos, sbObjects[PLAYER_OBJ_IDX].pos, sbObjects[idx].sightRange ) ) {
		//llog( LOG_DEBUG, "lost player" );
		aiEnterCurious( idx, sbObjects[PLAYER_OBJ_IDX].pos );
		return;
	}

	//llog( LOG_DEBUG, "staying aggresive" );
}

static void idleAI( size_t idx )
{
	// wander
	Object* obj = &( sbObjects[idx] );

	if( obj->counter > 0 ) {
		--obj->counter;
		return;
	}

	HexGridCoord target = hex_GetNeighbor( obj->pos, rand_GetRangeS32( NULL, 0, 5 ) );

	if( isMoveable( target ) ) {
		moveObjecTTo( idx, target );
	}

	obj->counter = rand_GetRangeS32( NULL, 3, 10 );
}

static size_t aStarSearcher;
float aStarCostFunc( void* graph, int fromNodeID, int toNodeID )
{
	float cost = 1.0f;
	HexGridCoord c = hex_Flat_RectIndexToCoord( toNodeID, MAP_WIDTH, MAP_HEIGHT );
	if( !isMoveableTwo( c, PLAYER_OBJ_IDX, aStarSearcher ) ) {
		cost = INFINITY;
	}

	return cost;
}

static float aStarHeuristic( void* graph, int fromNodeID, int toNodeID )
{
	HexGridCoord from = hex_Flat_RectIndexToCoord( fromNodeID, MAP_WIDTH, MAP_HEIGHT );
	HexGridCoord to = hex_Flat_RectIndexToCoord( toNodeID, MAP_WIDTH, MAP_HEIGHT );
	if( !isMoveableTwo( to, PLAYER_OBJ_IDX, aStarSearcher ) ) {
		return INFINITY;
	}

	return (float)hex_Distance( from, to );
}

static int sStarNextNeighbor( void* graph, int node, int currNeighborIdx )
{
	// find the next valid neighbor that isn't off the map
	HexGridCoord base = hex_Flat_RectIndexToCoord( node, MAP_WIDTH, MAP_HEIGHT );

	int offsetIdx = -1;
	HexGridCoord n;
	if( currNeighborIdx != -1 ) {
		// find current while
		HexGridCoord curr = hex_Flat_RectIndexToCoord( currNeighborIdx, MAP_WIDTH, MAP_HEIGHT );
		do {
			++offsetIdx;
			n = hex_GetNeighbor( base, offsetIdx );
		} while( !compareCoords( &n, &curr ) );
	}

	do {
		++offsetIdx;

		// past the end
		if( offsetIdx >= 6 ) {
			return -1;
		}

		n = hex_GetNeighbor( base, offsetIdx );
	} while( !hex_Flat_CoordInRect( n, MAP_WIDTH, MAP_HEIGHT ) );

	return hex_Flat_CoordToRectIndex( n, MAP_WIDTH, MAP_HEIGHT );
}

static int* sbSearchPath = NULL;
static bool pathToSpot( size_t searcher, HexGridCoord target, HexGridCoord* out )
{
	AStarSearchState searchState;
	int startIdx = hex_Flat_CoordToRectIndex( sbObjects[searcher].pos, MAP_WIDTH, MAP_HEIGHT );
	int endIdx = hex_Flat_CoordToRectIndex( target, MAP_WIDTH, MAP_HEIGHT );
	aStarSearcher = searcher;
	aStar_CreateSearchState( (void*)map, ARRAY_SIZE( map ), startIdx, endIdx, aStarCostFunc, aStarHeuristic, sStarNextNeighbor, &searchState );
	aStar_ProcessPath( &searchState, -1, &sbSearchPath );

	bool found = false;
	if( sb_Count( sbSearchPath ) > 0 ) {
		(*out) = hex_Flat_RectIndexToCoord( sb_Last( sbSearchPath ), MAP_WIDTH, MAP_HEIGHT );
		found = true;
	}

	aStar_CleanUpSearchState( &searchState );
	sb_Clear( sbSearchPath );

	return found;
}

static void curiousAI( size_t idx )
{
	// seek target
	HexGridCoord move;
	if( pathToSpot( idx, sbObjects[idx].curiousSpot, &move ) ) {
		moveObjecTTo( idx, move );
	} else {
		// no path, switch to idle
		aiEnterIdle( idx );
	}
}

static void aggressiveAI( size_t idx )
{
	// if we're right next to them then attack them
	if( hex_Distance( sbObjects[idx].pos, sbObjects[PLAYER_OBJ_IDX].pos ) <= sbObjects[idx].attackRange ) {
		assert( sbObjects[idx].attackAction );
		sbObjects[idx].attackAction( idx );
	} else {
		// seek target
		HexGridCoord move;
		if( pathToSpot( idx, sbObjects[PLAYER_OBJ_IDX].pos, &move ) ) {
			moveObjecTTo( idx, move );
		} else {
			// no path, switch to idle
			aiEnterIdle( idx );
		}
	}
}

static void standardEnemyAI( size_t idx )
{
	Object* obj = &( sbObjects[idx] );
	if( obj->health <= 0 ) return;

	// senses
	switch( obj->state ) {
		case OS_IDLE:
			idleAISense( idx );
			break;
		case OS_CURIOUS:
			curiousAISense( idx );
			break;
		case OS_AGGRESIVE:
			aggresiveAISense( idx );
			break;
	}

	// then decisions
	switch( obj->state ) {
	case OS_IDLE:
		idleAI( idx );
		break;
	case OS_CURIOUS:
		curiousAI( idx );
		break;
	case OS_AGGRESIVE:
		aggressiveAI( idx );
		break;
	}//*/

	aq_PushToEndOfQueue( idx );
}

static void damagePlayer( int damage, size_t attacker, const char* attackDesc, int* soundOut )
{
	// choose what to hit
	Part* partToHit = NULL;
	do {
		int choice = rand_GetRangeS32( NULL, 0, 5 );
		switch( choice ) {
		case 0:
			partToHit = &( player.body );
			break;
		case 1:
			partToHit = &( player.melee );
			break;
		case 2:
			partToHit = &( player.ranged );
			break;
		case 3:
			partToHit = &( player.legs );
			break;
		case 4:
			partToHit = &( player.sensor );
			break;
		case 5:
			partToHit = &( player.smoke );
			break;
		}
	} while( partToHit->health <= 0 );

	
	damage = MIN( damage, partToHit->health );
	partToHit->health -= damage;

	char buffer[64];
	// different text on if we're dead or not
	if( ( player.body.health <= 0 ) && ( player.melee.health <= 0 ) && ( player.ranged.health <= 0 ) && ( player.legs.health <= 0 ) && ( player.sensor.health <= 0 ) && ( player.smoke.health <= 0 ) ) {
		// dead
		if( soundOut ) ( *soundOut ) = playerDeadSnd;
		SDL_snprintf( buffer, 63, "The %s tears through your mech, leaving you dead.", attackDesc );
		appendToTextBuffer( buffer );
		sbObjects[PLAYER_OBJ_IDX].state = OS_DEAD;
		inputType = IT_LOST;
	} else {
		// hit
		SDL_snprintf( buffer, 63, "The %s hits you in the %s for %i damage.", attackDesc, partToHit->name, damage );
		if( soundOut ) ( *soundOut ) = playerDamageSnd;
		appendToTextBuffer( buffer );

		if( partToHit->health <= 0 ) {
			if( soundOut ) ( *soundOut ) = playerBreakSnd;
			SDL_snprintf( buffer, 63, "Your %s has been disabled!", partToHit->name );
			appendToTextBuffer( buffer );
		}
	}
}

static void standardEnemyRangedAttack( size_t idx )
{
	char buffer[64];

	if( sbObjects[PLAYER_OBJ_IDX].immunityTurns > 0 ) {
		SDL_snprintf( buffer, 63, "The %s's shot bounces off your shield.", sbObjects[idx].name );
		appendToTextBuffer( buffer );
		return;
	}

	int playerDodge;
	int playerArmor;

	if( player.legs.health <= 0 ) {
		playerDodge = player.damagedLegsDodge;
	} else {
		playerDodge = player.baseDodge;
	}

	if( player.body.health <= 0 ) {
		playerArmor = player.damagedBodyArmor;
	} else {
		playerArmor = player.baseArmor;
	}

	int damageSound = -1;

	if( rollDicePool( sbObjects[idx].hitDice ) >= rollDicePool( playerDodge ) ) {
		int damage = rollDicePool( sbObjects[idx].damageDice ) - rollDicePool( playerArmor );
		if( damage > 0 ) {
			damagePlayer( damage, idx, "shot", &damageSound );
			damageSound = playerDamageSnd;
		} else {
			SDL_snprintf( buffer, 63, "The %s's shot glances off you.", sbObjects[idx].name );
			appendToTextBuffer( buffer );
		}
	} else {
		SDL_snprintf( buffer, 63, "The %s misses you.", sbObjects[idx].name );
		appendToTextBuffer( buffer );
	}

	createProjectileObject( sbObjects[idx].pos, sbObjects[PLAYER_OBJ_IDX].pos, PLAYER_OBJ_IDX, damageSound );
}

static void standardEnemyMeleeAttack( size_t idx )
{
	char buffer[64];

	if( sbObjects[PLAYER_OBJ_IDX].immunityTurns > 0 ) {
		SDL_snprintf( buffer, 63, "The %s's claw bounces off your shield.", sbObjects[idx].name );
		appendToTextBuffer( buffer );
		return;
	}

	int playerDodge;
	int playerArmor;

	if( player.legs.health <= 0 ) {
		playerDodge = player.damagedLegsDodge;
	} else {
		playerDodge = player.baseDodge;
	}

	if( player.body.health <= 0 ) {
		playerArmor = player.damagedBodyArmor;
	} else {
		playerArmor = player.baseArmor;
	}

	meleeAttackWithObj( idx, sbObjects[PLAYER_OBJ_IDX].pos );

	int damageSound = -1;

	if( rollDicePool( sbObjects[idx].hitDice ) >= rollDicePool( playerDodge ) ) {
		int damage = rollDicePool( sbObjects[idx].damageDice ) - rollDicePool( playerArmor );
		if( damage > 0 ) {
			shakeObject( PLAYER_OBJ_IDX );
			damagePlayer( damage, idx, "claw", &damageSound );
		} else {
			SDL_snprintf( buffer, 63, "The %s's claw glances off you.", sbObjects[idx].name );
			appendToTextBuffer( buffer );
		}
	} else {
		SDL_snprintf( buffer, 63, "The %s misses you.", sbObjects[idx].name );
		appendToTextBuffer( buffer );
	}

	playSound( damageSound );
}

static void deathExplosion( size_t idx )
{
	playSound( explosionSnd );
	HexGridCoord base = sbObjects[idx].pos;
	char buffer[64];

	SDL_snprintf( buffer, 63, "The %s explodes!", sbObjects[idx].name );
	appendToTextBuffer( buffer );

	// damage everthing around us, no chance of dodge
	for( int i = 0; i < 6; ++i ) {
		HexGridCoord n = hex_GetNeighbor( base, i );

		size_t found = getObjectAt( n, true );
		if( found != SIZE_MAX ) {
			if( found == PLAYER_OBJ_IDX ) {
				if( sbObjects[PLAYER_OBJ_IDX].immunityTurns > 0 ) {
					appendToTextBuffer( "Your shield protects you from the explosion." );
				} else {
					int playerArmor;
					if( player.body.health <= 0 ) {
						playerArmor = player.damagedBodyArmor;
					} else {
						playerArmor = player.baseArmor;
					}
					int damage = rollDicePool( sbObjects[idx].damageDice ) - rollDicePool( playerArmor );
					if( damage > 0 ) {
						shakeObject( PLAYER_OBJ_IDX );
						damagePlayer( damage, idx, "explosion", NULL );
					} else {
						appendToTextBuffer( "Your armor absorbs the explosion." );
					}
				}

			} else {
				int damage = rollDicePool( sbObjects[idx].damageDice ) - rollDicePool( sbObjects[found].armorDice );
				if( damage > 0 ) {
					sbObjects[found].health -= damage;
					if( sbObjects[found].health <= 0 ) {
						SDL_snprintf( buffer, 63, "The %s is killed by the explosion.", sbObjects[found].name );
						appendToTextBuffer( buffer );
					} else {
						SDL_snprintf( buffer, 63, "The %s is hit by the explosion for %i damage.", sbObjects[found].name, damage );
						appendToTextBuffer( buffer );
					}
				} else {
					SDL_snprintf( buffer, 63, "The %s is unscathed by the explosion.", sbObjects[found].name );
					appendToTextBuffer( buffer );
				}
			}
		}
	}
}

static void alarmEnemyAI( size_t idx )
{
	Object* obj = &( sbObjects[idx] );
	if( obj->health <= 0 ) return;

	--obj->counter;
	if( obj->counter <= 0 ) {
		if( isVisible( obj->pos, sbObjects[PLAYER_OBJ_IDX].pos, obj->sightRange ) ) {
			// shriek
			char buffer[64];
			SDL_snprintf( buffer, 63, "The %s lets out a loud shriek.", obj->name );
			appendToTextBuffer( buffer );

			for( size_t i = 0; i < sb_Count( sbObjects ); ++i ) {
				if( sbObjects[i].attackAction == NULL ) continue;
				aiEnterCurious( i, obj->pos );
			}
		}

		obj->counter = rand_GetRangeS32( NULL, 2, 4 );
	}

	aq_PushToEndOfQueue( idx );
}