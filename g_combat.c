// g_combat.c

#include "g_local.h"
#include "z_gq.h"

void create_fireball (edict_t *target, edict_t *owner);
/*
============
CanDamage

Returns true if the inflictor can directly damage the target.  Used for
explosions and melee attacks.
============
*/
qboolean CanDamage (edict_t *targ, edict_t *inflictor)
{
	vec3_t	dest;
	trace_t	trace;

// bmodels need special checking because their origin is 0,0,0
	if (targ->movetype == MOVETYPE_PUSH)
	{
		VectorAdd (targ->absmin, targ->absmax, dest);
		VectorScale (dest, 0.5, dest);
		trace = gi.trace (inflictor->s.origin, vec3_origin, vec3_origin, dest, inflictor, MASK_SOLID);
		if (trace.fraction == 1.0)
			return true;
		if (trace.ent == targ)
			return true;
		return false;
	}
	
	trace = gi.trace (inflictor->s.origin, vec3_origin, vec3_origin, targ->s.origin, inflictor, MASK_SOLID);
	if (trace.fraction == 1.0)
		return true;

	VectorCopy (targ->s.origin, dest);
	dest[0] += 15.0;
	dest[1] += 15.0;
	trace = gi.trace (inflictor->s.origin, vec3_origin, vec3_origin, dest, inflictor, MASK_SOLID);
	if (trace.fraction == 1.0)
		return true;

	VectorCopy (targ->s.origin, dest);
	dest[0] += 15.0;
	dest[1] -= 15.0;
	trace = gi.trace (inflictor->s.origin, vec3_origin, vec3_origin, dest, inflictor, MASK_SOLID);
	if (trace.fraction == 1.0)
		return true;

	VectorCopy (targ->s.origin, dest);
	dest[0] -= 15.0;
	dest[1] += 15.0;
	trace = gi.trace (inflictor->s.origin, vec3_origin, vec3_origin, dest, inflictor, MASK_SOLID);
	if (trace.fraction == 1.0)
		return true;

	VectorCopy (targ->s.origin, dest);
	dest[0] -= 15.0;
	dest[1] -= 15.0;
	trace = gi.trace (inflictor->s.origin, vec3_origin, vec3_origin, dest, inflictor, MASK_SOLID);
	if (trace.fraction == 1.0)
		return true;


	return false;
}


/*
============
Killed
============
*/
void Killed (edict_t *targ, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
//	debugmsg("Func: Killed\n");
	if (targ->health < -999)
		targ->health = -999;

	targ->enemy = attacker;
	if ((targ->svflags & SVF_MONSTER) && (targ->deadflag != DEAD_DEAD))
	{
//		targ->svflags |= SVF_DEADMONSTER;	// now treat as a different content type
		if (!(targ->monsterinfo.aiflags & AI_GOOD_GUY))
		{
			level.killed_monsters++;
			if (coop->value && attacker->client) {
				attacker->client->resp.score++;
			}
			// medics won't heal monsters that they kill themselves
			if (strcmp(attacker->classname, "monster_medic") == 0)
				targ->owner = attacker;
		}
	}

	if (targ->movetype == MOVETYPE_PUSH || targ->movetype == MOVETYPE_STOP || targ->movetype == MOVETYPE_NONE)
	{	// doors, triggers, etc
		targ->die (targ, inflictor, attacker, damage, point);
		return;
	}

	if ((targ->svflags & SVF_MONSTER) && (targ->deadflag != DEAD_DEAD))
	{
		targ->touch = NULL;
		monster_death_use (targ);
	}

	targ->die (targ, inflictor, attacker, damage, point);
}


/*
================
SpawnDamage
================
*/
void SpawnDamage (int type, vec3_t origin, vec3_t normal, int damage)
{
	if (damage > 255)
		damage = 255;
	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (type);
//	gi.WriteByte (damage);
	gi.WritePosition (origin);
	gi.WriteDir (normal);
	gi.multicast (origin, MULTICAST_PVS);
}


/*
============
T_Damage

targ		entity that is being damaged
inflictor	entity that is causing the damage
attacker	entity that caused the inflictor to damage targ
	example: targ=monster, inflictor=rocket, attacker=player

dir			direction of the attack
point		point at which the damage is being inflicted
normal		normal vector from that point
damage		amount of damage being inflicted
knockback	force to be applied against targ as a result of the damage

dflags		these flags are used to control how T_Damage works
	DAMAGE_RADIUS			damage was indirect (from a nearby explosion)
	DAMAGE_NO_ARMOR			armor does not protect from this damage
	DAMAGE_ENERGY			damage is from an energy based weapon
	DAMAGE_NO_KNOCKBACK		do not affect velocity, just view angles
	DAMAGE_BULLET			damage is from a bullet (used for ricochets)
	DAMAGE_NO_PROTECTION	kills godmode, armor, everything
============
*/
static int CheckPowerArmor (edict_t *ent, vec3_t point, vec3_t normal, int damage, int dflags)
{
	gclient_t	*client;
	int			save;
	int			power_armor_type;
	int			index;
	int			damagePerCell;
	int			pa_te_type;
	int			power;
	int			power_used;

	if (!damage)
		return 0;

	client = ent->client;

	if (dflags & DAMAGE_NO_ARMOR)
		return 0;

	if (client)
	{
		power_armor_type = PowerArmorType (ent);
		if (power_armor_type != POWER_ARMOR_NONE)
		{
			index = ITEM_INDEX(FindItem("Cells"));
			power = client->pers.inventory[index];
		}
	}
	else if (ent->svflags & SVF_MONSTER)
	{
		power_armor_type = ent->monsterinfo.power_armor_type;
		power = ent->monsterinfo.power_armor_power;
	}
	else
		return 0;

	if (power_armor_type == POWER_ARMOR_NONE)
		return 0;
	if (!power)
		return 0;

	if (power_armor_type == POWER_ARMOR_SCREEN)
	{
		vec3_t		vec;
		float		dot;
		vec3_t		forward;

		// only works if damage point is in front
		AngleVectors (ent->s.angles, forward, NULL, NULL);
		VectorSubtract (point, ent->s.origin, vec);
		VectorNormalize (vec);
		dot = DotProduct (vec, forward);
		if (dot <= 0.3)
			return 0;

		damagePerCell = 1;
		pa_te_type = TE_SCREEN_SPARKS;
		damage = damage / 3;
	}
	else
	{
		damagePerCell = 2;
		pa_te_type = TE_SHIELD_SPARKS;
		damage = (2 * damage) / 3;
	}

	save = power * damagePerCell;
	if (!save)
		return 0;
	if (save > damage)
		save = damage;

	SpawnDamage (pa_te_type, point, normal, save);
	ent->powerarmor_time = level.time + 0.2;

	power_used = save / damagePerCell;

	if (client)
		client->pers.inventory[index] -= power_used;
	else
		ent->monsterinfo.power_armor_power -= power_used;
	return save;
}

static int CheckArmor (edict_t *ent, vec3_t point, vec3_t normal, int damage, int te_sparks, int dflags)
{
	gclient_t	*client;
	int			save;
	int			index;
	gitem_t		*armor;

	if (!damage)
		return 0;

	client = ent->client;

	if (!client)
		return 0;

	if (dflags & DAMAGE_NO_ARMOR)
		return 0;

	index = ArmorIndex (ent);
	if (!index)
		return 0;

	armor = GetItemByIndex (index);

	if (dflags & DAMAGE_ENERGY)
		save = ceil(((gitem_armor_t *)armor->info)->energy_protection*damage);
	else
		save = ceil(((gitem_armor_t *)armor->info)->normal_protection*damage);
	if (save >= client->pers.inventory[index])
		save = client->pers.inventory[index];

	if (!save)
		return 0;

	client->pers.inventory[index] -= save;
	SpawnDamage (te_sparks, point, normal, save);

	return save;
}

void M_ReactToDamage (edict_t *targ, edict_t *attacker)
{
	if (!(attacker->client) && !(attacker->svflags & SVF_MONSTER))
		return;

	if (attacker == targ || attacker == targ->enemy)
		return;

	// if we are a good guy monster and our attacker is a player
	// or another good guy, do not get mad at them
	if (targ->monsterinfo.aiflags & AI_GOOD_GUY)
	{
		if (attacker->client || (attacker->monsterinfo.aiflags & AI_GOOD_GUY))
			return;
	}

	// we now know that we are not both good guys

	// if attacker is a client, get mad at them because he's good and we're not
	if (attacker->client)
	{
		targ->monsterinfo.aiflags &= ~AI_SOUND_TARGET;

		// this can only happen in coop (both new and old enemies are clients)
		// only switch if can't see the current enemy
		if (targ->enemy && targ->enemy->client)
		{
			if (visible(targ, targ->enemy))
			{
				targ->oldenemy = attacker;
				return;
			}
			targ->oldenemy = targ->enemy;
		}
		targ->enemy = attacker;
		if (!(targ->monsterinfo.aiflags & AI_DUCKED))
			FoundTarget (targ);
		return;
	}

	// it's the same base (walk/swim/fly) type and a different classname and it's not a tank
	// (they spray too much), get mad at them
	if (((targ->flags & (FL_FLY|FL_SWIM)) == (attacker->flags & (FL_FLY|FL_SWIM))) &&
		 (strcmp (targ->classname, attacker->classname) != 0) &&
		 (strcmp(attacker->classname, "monster_tank") != 0) &&
		 (strcmp(attacker->classname, "monster_supertank") != 0) &&
		 (strcmp(attacker->classname, "monster_makron") != 0) &&
		 (strcmp(attacker->classname, "monster_jorg") != 0) )
	{
		if (targ->enemy && targ->enemy->client)
			targ->oldenemy = targ->enemy;
		targ->enemy = attacker;
		if (!(targ->monsterinfo.aiflags & AI_DUCKED))
			FoundTarget (targ);
	}
	// if they *meant* to shoot us, then shoot back
	else if (attacker->enemy == targ)
	{
		if (targ->enemy && targ->enemy->client)
			targ->oldenemy = targ->enemy;
		targ->enemy = attacker;
		if (!(targ->monsterinfo.aiflags & AI_DUCKED))
			FoundTarget (targ);
	}
	// otherwise get mad at whoever they are mad at (help our buddy) unless it is us!
	else if (attacker->enemy && attacker->enemy != targ)
	{
		if (targ->enemy && targ->enemy->client)
			targ->oldenemy = targ->enemy;
		targ->enemy = attacker->enemy;
		if (!(targ->monsterinfo.aiflags & AI_DUCKED))
			FoundTarget (targ);
	}
}

/*
CannotTeamDamage - True if the two CANNOT hurt each other -- Stone
Renamed from CheckTeamDamage
Returns false if players are on opposite teams or teamplay is off.
Returns false if the target and attacker are the same person.
Returns true if the players are on the same team and team damage is
enabled.
*/
qboolean CannotTeamDamage (edict_t *targ, edict_t *attacker)
{
	// Added by Stone
	// Can always harm self
	if (targ==attacker) {
//		debugmsg("Hit self\n");
		return false;
	}
	// Only players can be on teams
	if (!targ->client || !attacker->client) {
//		debugmsg("Not a player\n");
		return false;
	}
	if (team_damage->value) {
//		debugmsg("Team damage is on, so that's no defense.\n");
		return false;
	}
	if (!deathmatch->value) {
//		debugmsg("We're not in deathmatch.\n");
		return false;
	}
	if (!teamplay->value && ((int)playmode->value != PM_BADGE_WARS)) {
//		debugmsg("No teams in this mode\n");
		return false;
	}
	return OnSameTeam(targ,attacker);

		//FIXME make the next line real and uncomment this block
		// if ((ability to damage a teammate == OFF) && (targ's team == attacker's team))
		// return false;
}

void T_Damage (edict_t *targ, edict_t *inflictor, edict_t *attacker, vec3_t dir, vec3_t point, vec3_t normal, int damage, int knockback, int dflags, int mod)
{
	gclient_t	*client;
	int			take;
	int			save;
	int			asave;
	int			psave;
	int			te_sparks;

//	debugmsg("Func: T_Damage\n");
	if (!targ->takedamage)
		return;

	// friendly fire avoidance
	// if enabled you can't hurt teammates (but you can hurt yourself)
	// knockback still occurs
	meansOfDeath = mod;
	if ((targ != attacker) && (deathmatch->value || coop->value))
	{
		if (OnSameTeam (targ, attacker))
		{
//			if ((int)(dmflags->value) & DF_NO_FRIENDLY_FIRE)
			if (!(team_damage->value)) {
				damage = 0;
//				debugmsg("Team damage is disabled.\n");
			} else
				meansOfDeath |= MOD_FRIENDLY_FIRE;
		}
	}

	// easy mode takes half damage
	if (skill->value == 0 && deathmatch->value == 0 && targ->client)
	{
		damage *= 0.5;
		if (!damage)
			damage = 1;
	}

	client = targ->client;

	if (dflags & DAMAGE_BULLET)
		te_sparks = TE_BULLET_SPARKS;
	else
		te_sparks = TE_SPARKS;

	VectorNormalize(dir);
// bonus damage for suprising a monster
	if (!(dflags & DAMAGE_RADIUS) && (targ->svflags & SVF_MONSTER) && (attacker->client) && (!targ->enemy) && (targ->health > 0))
		damage *= 2;

	if (targ->flags & FL_NO_KNOCKBACK)
		knockback = 0;

// figure momentum add
	if (!(dflags & DAMAGE_NO_KNOCKBACK))
	{
		if ((knockback) && (targ->movetype != MOVETYPE_NONE) && (targ->movetype != MOVETYPE_BOUNCE) && (targ->movetype != MOVETYPE_PUSH) && (targ->movetype != MOVETYPE_STOP))
		{
			vec3_t	kvel;
			float	mass;

			if (targ->mass < 50)
				mass = 50;
			else
				mass = targ->mass;

			if (targ->client  && attacker == targ)
				VectorScale (dir, 1600.0 * (float)knockback / mass, kvel);	// the rocket jump hack...
			else
				VectorScale (dir, 500.0 * (float)knockback / mass, kvel);

			VectorAdd (targ->velocity, kvel, targ->velocity);
		}
	}

	take = damage;
	save = 0;

	// check for godmode
	if ( (targ->flags & FL_GODMODE) && !(dflags & DAMAGE_NO_PROTECTION) )
	{
		take = 0;
		save = damage;
		SpawnDamage (te_sparks, point, normal, save);
	}

	// check for invincibility
	if ((client && client->invincible_framenum > level.framenum ) && !(dflags & DAMAGE_NO_PROTECTION))
	{
		if (targ->pain_debounce_time < level.time)
		{
			gi.sound(targ, CHAN_ITEM, gi.soundindex("items/protect4.wav"), 1, ATTN_NORM, 0);
			targ->pain_debounce_time = level.time + 2;
		}
		take = 0;
		save = damage;
	}

	// Check for respawn invulnerability

	if (deathmatch->value && client && respawn_invuln->value && (targ->client->respawn_framenum > level.framenum))
	{
		if (targ->pain_debounce_time < level.time)
		{
			gi.sound(targ, CHAN_ITEM, gi.soundindex("items/protect4.wav"), 1, ATTN_NORM, 0);
			targ->pain_debounce_time = level.time + 2;
		}
		take = 0;
		save = damage;
	}

	// During Last Man Standing preptime?
	if (deathmatch->value && ((int)playmode->value == PM_LAST_MAN_STANDING) && (preptimeframe > level.framenum))
		take=0;

//	psave = CheckPowerArmor (targ, point, normal, take, dflags);
//	take -= psave;
	psave = 0;

//	asave = CheckArmor (targ, point, normal, take, te_sparks, dflags);
//	take -= asave;
	asave = 0;

	//treat cheat/powerup savings the same as armor
	asave += save;

	// Locational damage -- Added by Stone
	if (hit_locations->value && !(dflags & DAMAGE_RADIUS) && ((targ->svflags & SVF_MONSTER) || targ->client) && targ->pain && !(targ->flags & (FL_FLY|FL_SWIM)) && !(targ->svflags & SVF_DEADMONSTER)) {
		float height;
//		height=targ->absmax[2] - targ->absmin[2];
		height=targ->maxs[2] - targ->mins[2];
//		debugmsg("Point z: %.1f  Min z: %.1f  Height: %.1f\n", point[2], targ->s.origin[2], height);
//		debugmsg("Checking hit location...\n");
		if (point[2] < (targ->absmin[2] + (height/2))) {
//			debugmsg("Leg shot\n");
			take = take * .5;		// Leg shot
		}
		else if (point[2] > (targ->absmax[2] - (height/6))) {
			if (attacker->client)
				attacker->client->resp.standard[GSTAT_HEADSHOT]+=take;
//			debugmsg("Headshot score: %i\n", attacker->client->resp.standard[GSTAT_HEADSHOT]);
			take = take * 2;		// Head shot
//			debugmsg("Head shot.\n");
			// MONSTER'S DON'T HAVE CLIENTS
			if (client && attacker->client && (client!=attacker->client)) {
				meansOfDeath |= MOD_HEAD_SHOT;
			}
		}
	}
	// End of locational damage

	// team damage avoidance
	if (!(dflags & DAMAGE_NO_PROTECTION) && CannotTeamDamage (targ, attacker)) {
//		debugmsg("Team damage avoidance\n");
		return;
	}

// do the damage
	if (take)
	{
		if ((targ->svflags & SVF_MONSTER) || (client))
			if ((mod!=MOD_FIRE) && (mod!=MOD_POISON))
				SpawnDamage (TE_BLOOD, point, normal, take);
		else
			SpawnDamage (te_sparks, point, normal, take);

//		debugmsg("Damage against %s: %i\n",targ->classname, take);
		targ->health = targ->health - take;

		if (attacker->client && !(targ->svflags & SVF_DEADMONSTER) && GQ_MatchItem(attacker->client->artifact,"Tongue of the Leech") && (attacker->health < 200)) {
			attacker->health+=(int)(take/2);
			if (attacker->health > 200)
				attacker->health = 200;
		}
		if (attacker && attacker->client && targ->client && GQ_MatchItem(attacker->client->artifact,"Tail of the Scorpion") && (mod!=MOD_POISON)) {
			if (targ->client->poison) {
				targ->client->poison->count+=(int)(take/2);
			} else {
				edict_t *poison=G_Spawn();
				poison->classname="Poison";
				poison->owner=attacker;
				poison->enemy=targ;
				poison->think=GQ_PoisonThink;
				poison->nextthink=level.time+.5;
				poison->count=(int)(take/2);
				targ->client->poison=poison;
			}
		}

		if (targ->health <= 0)
		{
			if ((targ->svflags & SVF_MONSTER) || (client))
				targ->flags |= FL_NO_KNOCKBACK;
			Killed (targ, inflictor, attacker, take, point);
			return;
		}
	}

	if (targ->svflags & SVF_MONSTER)
	{
		M_ReactToDamage (targ, attacker);
		if (!(targ->monsterinfo.aiflags & AI_DUCKED) && (take))
		{
			targ->pain (targ, attacker, knockback, take);
			// nightmare mode monsters don't go into pain frames often
			if (skill->value == 3)
				targ->pain_debounce_time = level.time + 5;
		}
	}
	else if (client)
	{
		if (!(targ->flags & FL_GODMODE) && take && targ->pain)
			targ->pain (targ, attacker, knockback, take);
	}
	else if (take)
	{
		if (targ->pain)
			targ->pain (targ, attacker, knockback, take);
	}

	// add to the damage inflicted on a player this frame
	// the total will be turned into screen blends and view angle kicks
	// at the end of the frame
	if (client)
	{
		client->damage_parmor += psave;
		client->damage_armor += asave;
		client->damage_blood += take;
		client->damage_knockback += knockback;
		VectorCopy (point, client->damage_from);
	}
}


/*
============
T_RadiusDamage
============
*/
void T_RadiusDamage (edict_t *inflictor, edict_t *attacker, float damage, edict_t *ignore, float radius, int mod)
{
	float	points;
	edict_t	*ent = NULL;
	vec3_t	v;
	vec3_t	dir;

	while ((ent = findradius(ent, inflictor->s.origin, radius)) != NULL)
	{
		if (ent == ignore)
			continue;
		if (!ent->takedamage)
			continue;

		VectorAdd (ent->mins, ent->maxs, v);
		VectorMA (ent->s.origin, 0.5, v, v);
		VectorSubtract (inflictor->s.origin, v, v);
		points = damage - 0.5 * VectorLength (v);
		if (ent == attacker)
			points = points * 0.5;
		if (points > 0)
		{
			if (CanDamage (ent, inflictor))
			{
				VectorSubtract (ent->s.origin, inflictor->s.origin, dir);
//				debugmsg("Radius damage %s: ",ent->classname);
				if (attacker->client && ent->health>0)
					attacker->client->resp.standard[GSTAT_HITS]++;
				T_Damage (ent, inflictor, attacker, dir, inflictor->s.origin, vec3_origin, (int)points, (int)points, DAMAGE_RADIUS, mod);
				if ((mod & MOD_HG_SPLASH) && !CannotTeamDamage(ent, attacker)) {
					create_fireball (ent, attacker);
				}
			}
		}
	}
}
