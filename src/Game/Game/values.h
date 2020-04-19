#ifndef VALUES_H
#define VALUES_H

#include <stdbool.h>

//*****************************************
// Player
typedef struct {
	int health;
	int maxHealth;
	const char* name;
	const char* overdriveDesc;
	const char* shortOverdriveDesc;
	const char* desc;
	const char* damagedDesc;
	const char* shortDamagedDesc;
	float chanceOfDamage;
} Part;

typedef struct {
	Part ranged;
	Part melee;
	Part body;
	Part legs;
	Part sensor;

	Part smoke;

	int meleeHitDice;
	int meleeDamageDice;

	int damagedMeleeHitDice;
	int damagedMeleeDamageDice;

	int rangedHitDice;
	int rangedDamageDice;

	int boostedRangegdHitDice;
	int boostedRangedDamageDice;

	int smokeDuration;

	int baseDodge;
	int baseArmor;

	int damagedLegsDodge;
	int damagedBodyArmor;

	int sensorDuration;
	int sensorBonus;
	int sensorDamagePenalty;
} PlayerStats;

extern PlayerStats player;

void initPlayer( void );


//*****************************************
// Assets
extern int playerImg;
extern int* sbBoxBorder;
extern int smallTreeImg;
extern int smallTreeDeadImg;
extern int projectileImg;
extern int shieldImg;
extern int smokeImg;
extern int playerDeadImg;

extern int weakNmyImg;
extern int weakNmyDeadImg;
extern int strongNmyImg;
extern int strongNmyDeadImg;
extern int armorNmyImg;
extern int armorNmyDeadImg;
extern int rangeNmyImg;
extern int rangeNmyDeadImg;
extern int boomerNmyImg;
extern int boomerNmyDeadImg;
extern int alarmNmyImg;
extern int alarmNmyDeadImg;

extern int groundTile;
extern int waterTile;
extern int tileHilite;
extern int treeC;
extern int treeN;
extern int treeNE;
extern int treeSE;
extern int treeS;
extern int treeSW;
extern int treeNW;

extern int helpImg;

extern int music;

extern int playerBreakSnd;
extern int creatureShootSnd;
extern int creatureDeadSnd;
extern int playerDeadSnd;
extern int explosionSnd;
extern int creatureDamageSnd;
extern int playerDamageSnd;
extern int invalidSnd;
extern int playerShootSnd;
extern int playerRepairSnd;

extern int fontText;
extern int fontDisplay;

#define UI_CAMERA 1
#define UI_CAMERA_FLAGS ( 1 << 1 )

#define GAME_CAMERA 0
#define GAME_CAMERA_FLAGS ( 1 << 0 )

bool loadAssets( void );
//*****************************************

#endif