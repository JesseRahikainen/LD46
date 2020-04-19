#include "values.h"

#include "../Graphics/imageSheets.h"
#include "../UI/text.h"
#include "../System/platformLog.h"
#include "../Utils/stretchyBuffer.h"
#include "../Graphics/graphics.h"
#include "../Graphics/images.h"
#include "../sound.h"

//*****************************************
// Player
PlayerStats player;

void initPlayer( void )
{
	player.body.name = "Torso";
	player.body.desc = "Main frame for your mech.";
	player.body.damagedDesc = "Increased damage to all other parts.";
	player.body.overdriveDesc = "Create a shield that protects you for 2 turns.";
	player.body.shortOverdriveDesc = "Shield 2 Turns";
	player.body.shortDamagedDesc = "   -Absorb";
	player.body.maxHealth = 8;
	player.body.health = 8;
	player.body.chanceOfDamage = 0.75f;

	player.ranged.name = "Gun";
	player.ranged.desc = "Plasma based long range weaponary.";
	player.ranged.damagedDesc = "Unable to shoot.";
	player.ranged.overdriveDesc = "Shot with better aim and damage.";
	player.ranged.shortOverdriveDesc = "+Aim +Dmg";
	player.ranged.shortDamagedDesc = "   Broken";
	player.ranged.maxHealth = 8;
	player.ranged.health = 8;
	player.ranged.chanceOfDamage = 0.5f;

	player.melee.name = "Sword";
	player.melee.desc = "Plasma based short range weaponry.";
	player.melee.damagedDesc = "Reduced accuracy.";
	player.melee.overdriveDesc = "Damages all spaces around you.";
	player.melee.shortOverdriveDesc = "AOE Attack";
	player.melee.shortDamagedDesc = "   -Accuracy";
	player.melee.maxHealth = 8;
	player.melee.health = 8;
	player.melee.chanceOfDamage = 0.5f;

	player.legs.name = "Legs";
	player.legs.desc = "Bipedal support system.";
	player.legs.damagedDesc = "Increased chance of being hit.";
	player.legs.overdriveDesc = "Lets you move twice.";
	player.legs.shortOverdriveDesc = "Two Moves";
	player.legs.shortDamagedDesc = "   -Dodge";
	player.legs.maxHealth = 8;
	player.legs.health = 8;
	player.legs.chanceOfDamage = 0.5f;

	player.sensor.name = "Sensors";
	player.sensor.desc = "Long range scanners.";
	player.sensor.damagedDesc = "Disable look command, decreased chance of hitting.";
	player.sensor.overdriveDesc = "Increased chance to hit for 3 turns.";
	player.sensor.shortOverdriveDesc = "+Aim 3 Turns";
	player.sensor.shortDamagedDesc = "   No Look";
	player.sensor.maxHealth = 8;
	player.sensor.health = 8;
	player.sensor.chanceOfDamage = 0.25f;

	player.smoke.name = "Smoke Screen";
	player.smoke.desc = "Tatical cover generator.";
	player.smoke.damagedDesc = "Unable to use.";
	player.smoke.overdriveDesc = "Creates a smoke screen at your position.";
	player.smoke.shortOverdriveDesc = "Smoke Screen";
	player.smoke.shortDamagedDesc = "   No Ammo";
	player.smoke.maxHealth = 4;
	player.smoke.health = 0;
	player.smoke.chanceOfDamage = 0.9f;

	player.meleeHitDice = 8;
	player.meleeDamageDice = 10;

	player.damagedMeleeHitDice = 6;
	player.damagedMeleeDamageDice = 6;

	player.rangedHitDice = 10;
	player.rangedDamageDice = 5;

	player.boostedRangegdHitDice = 15;
	player.boostedRangedDamageDice = 15;

	player.baseDodge = 8;
	player.baseArmor = 4;

	player.damagedLegsDodge = 4;
	player.damagedBodyArmor = 2;

	player.smokeDuration = 10;

	player.sensorBonus = 4;
	player.sensorDuration = 3;

	player.sensorDamagePenalty = 2;
}

//*****************************************
// Assets
#define LOAD_AND_TEST( file, func, id ) \
	{ ( id ) = func( file ); if( ( id ) < 0 ) { \
		llog( LOG_ERROR, "Error loading resource file %s.", file ); return false; } else { llog( LOG_INFO, "Successfully loaded %s.", file ); } }

#define LOAD_AND_TEST_IMG( file, shaderType, id ) \
	{ ( id ) = img_Load( file, shaderType ); if( ( id ) < 0 ) { \
		llog( LOG_ERROR, "Error loading image file %s.", file ); return false; } else { llog( LOG_INFO, "Successfully loaded image %s.", file ); } }

#define LOAD_AND_TEST_FNT( file, size, id ) \
	{ ( id ) = txt_LoadFont( file, size ); if( ( id ) < 0 ) { \
		llog( LOG_ERROR, "Error loading font file %s.", file ); return false; } else { llog( LOG_INFO, "Successfully loaded font %s.", file ); } }

#define LOAD_AND_TEST_SDF_FNT( file, id ) \
	{ ( id ) = txt_CreateSDFFont( file ); if( ( id ) < 0 ) { \
		llog( LOG_ERROR, "Error loading sdf font file %s.", file ); return false; } else { llog( LOG_INFO, "Successfully loaded sdf font %s.", file ); } }

#define LOAD_AND_TEST_SS( file, st, ids, cnt ) \
	{ ( ids ) = NULL; cnt = img_LoadSpriteSheet( file, st, &(ids) ); if( cnt < 0 ) { \
		llog( LOG_ERROR, "Error loading sprite sheet file %s.", file ); return false; } else { llog( LOG_INFO, "Successfully loaded sprite sheet %s.", file ); } }

#define LOAD_AND_TEST_MUSIC( file, id ) \
	{ ( id ) = snd_LoadStreaming( file, true, 0 ); if( ( id ) < 0 ) { \
		llog( LOG_ERROR, "Error loading streamed sound file %s.", file ); return false; } else { llog( LOG_INFO, "Successfully loaded music %s.", file ); } }

#define LOAD_AND_TEST_SOUND( file, id ) \
	{ ( id ) = snd_LoadSample( file, 1, false ); if( ( id ) < 0 ) { \
		llog( LOG_ERROR, "Error loading sample sound file file %s.", file ); return false; } else { llog( LOG_INFO, "Successfully loaded sound %s.", file ); } }

int playerImg = -1;
int* sbBoxBorder = NULL;
int smallTreeImg = -1;
int smallTreeDeadImg = -1;
int projectileImg = -1;
int shieldImg = -1;
int smokeImg = -1;
int playerDeadImg = -1;

int weakNmyImg = -1;
int weakNmyDeadImg = -1;
int strongNmyImg = -1;
int strongNmyDeadImg = -1;
int armorNmyImg = -1;
int armorNmyDeadImg = -1;
int rangeNmyImg = -1;
int rangeNmyDeadImg = -1;
int boomerNmyImg = -1;
int boomerNmyDeadImg = -1;
int alarmNmyImg = -1;
int alarmNmyDeadImg = -1;

int groundTile = -1;
int waterTile = -1;
int tileHilite = -1;
int treeC = -1;
int treeN = -1;
int treeNE = -1;
int treeSE = -1;
int treeS = -1;
int treeSW = -1;
int treeNW = -1;

int helpImg = -1;

int music = -1;

int playerBreakSnd = -1;
int creatureShootSnd = -1;
int creatureDeadSnd = -1;
int playerDeadSnd = -1;
int explosionSnd = -1;
int creatureDamageSnd = -1;
int playerDamageSnd = -1;
int invalidSnd = -1;
int playerShootSnd = -1;
int playerRepairSnd = -1;

int fontText = -1;
int fontDisplay = -1;

bool loadAssets( void )
{
	int count;

	LOAD_AND_TEST_IMG( "Images/player_mech.png", ST_DEFAULT, playerImg );
	LOAD_AND_TEST_IMG( "Images/player_mech_dead.png", ST_DEFAULT, playerDeadImg );
	LOAD_AND_TEST_SS( "Images/display_box.ss", ST_DEFAULT, sbBoxBorder, count );
	LOAD_AND_TEST_IMG( "Images/small_tree.png", ST_DEFAULT, smallTreeImg );
	LOAD_AND_TEST_IMG( "Images/small_tree_dead.png", ST_DEFAULT, smallTreeDeadImg );
	LOAD_AND_TEST_IMG( "Images/projectile.png", ST_DEFAULT, projectileImg );
	LOAD_AND_TEST_IMG( "Images/shield.png", ST_DEFAULT, shieldImg );
	LOAD_AND_TEST_IMG( "Images/smoke.png", ST_DEFAULT, smokeImg );

	LOAD_AND_TEST_IMG( "Images/weak_enemy.png", ST_DEFAULT, weakNmyImg );
	LOAD_AND_TEST_IMG( "Images/weak_enemy_dead.png", ST_DEFAULT, weakNmyDeadImg );
	LOAD_AND_TEST_IMG( "Images/strong_enemy.png", ST_DEFAULT, strongNmyImg );
	LOAD_AND_TEST_IMG( "Images/strong_enemy_dead.png", ST_DEFAULT, strongNmyDeadImg );
	LOAD_AND_TEST_IMG( "Images/armored_enemy.png", ST_DEFAULT, armorNmyImg );
	LOAD_AND_TEST_IMG( "Images/armored_enemy_dead.png", ST_DEFAULT, armorNmyDeadImg );
	LOAD_AND_TEST_IMG( "Images/spitter_enemy.png", ST_DEFAULT, rangeNmyImg );
	LOAD_AND_TEST_IMG( "Images/spitter_enemy_dead.png", ST_DEFAULT, rangeNmyDeadImg );
	LOAD_AND_TEST_IMG( "Images/explosive_enemy.png", ST_DEFAULT, boomerNmyImg );
	LOAD_AND_TEST_IMG( "Images/explosive_enemy_dead.png", ST_DEFAULT, boomerNmyDeadImg );
	LOAD_AND_TEST_IMG( "Images/shrieker_enemy.png", ST_DEFAULT, alarmNmyImg );
	LOAD_AND_TEST_IMG( "Images/shrieker_enemy_dead.png", ST_DEFAULT, alarmNmyDeadImg );

	LOAD_AND_TEST_IMG( "Images/hex_water.png", ST_DEFAULT, waterTile );
	LOAD_AND_TEST_IMG( "Images/hex_ground.png", ST_DEFAULT, groundTile );
	LOAD_AND_TEST_IMG( "Images/hex_hilite.png", ST_DEFAULT, tileHilite );

	LOAD_AND_TEST_IMG( "Images/hex_tree_c.png", ST_DEFAULT, treeC );
	LOAD_AND_TEST_IMG( "Images/hex_tree_n.png", ST_DEFAULT, treeN );
	LOAD_AND_TEST_IMG( "Images/hex_tree_ne.png", ST_DEFAULT, treeNE );
	LOAD_AND_TEST_IMG( "Images/hex_tree_se.png", ST_DEFAULT, treeSE );
	LOAD_AND_TEST_IMG( "Images/hex_tree_s.png", ST_DEFAULT, treeS );
	LOAD_AND_TEST_IMG( "Images/hex_tree_sw.png", ST_DEFAULT, treeSW );
	LOAD_AND_TEST_IMG( "Images/hex_tree_nw.png", ST_DEFAULT, treeNW );

	LOAD_AND_TEST_IMG( "Images/help.png", ST_DEFAULT, helpImg );

	//LOAD_AND_TEST_SDF_FNT( "Fonts/OpenSans-Regular.ttf", fontText );
	LOAD_AND_TEST_SDF_FNT( "Fonts/kenpixel_mini.ttf", fontText );

	LOAD_AND_TEST_MUSIC( "Sounds/bu-puppies-of-the-goats.ogg", music );

	LOAD_AND_TEST_SOUND( "Sounds/broke_player.ogg", playerBreakSnd );
	LOAD_AND_TEST_SOUND( "Sounds/creature_shoot.ogg", creatureShootSnd );
	LOAD_AND_TEST_SOUND( "Sounds/dead_creature.ogg", creatureDeadSnd );
	LOAD_AND_TEST_SOUND( "Sounds/dead_player.ogg", playerDeadSnd );
	LOAD_AND_TEST_SOUND( "Sounds/explosion.ogg", explosionSnd );
	LOAD_AND_TEST_SOUND( "Sounds/hurt_creature.ogg", creatureDamageSnd );
	LOAD_AND_TEST_SOUND( "Sounds/hurt_player.ogg", playerDamageSnd );
	LOAD_AND_TEST_SOUND( "Sounds/invalid.ogg", invalidSnd );
	LOAD_AND_TEST_SOUND( "Sounds/player_shoot.ogg", playerShootSnd );
	LOAD_AND_TEST_SOUND( "Sounds/repair.ogg", playerRepairSnd );

	return true;
}