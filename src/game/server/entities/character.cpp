/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <new>
#include <engine/shared/config.h>
#include <game/server/gamecontext.h>
#include <game/mapitems.h>

#include "character.h"
#include "laser.h"
#include "projectile.h"
#include "4laserbox.h"
#include "wall.h"
#include "pickup.h"



#include "plasma.h"
#include <stdio.h>
#include <string.h>
#include <engine/server/server.h>
#include <game/server/gamemodes/DDRace.h>
#include <game/server/score.h>
#include "light.h"
#include "door.h"
#include <game/layers.h>

//input count
struct CInputCount
{
	int m_Presses;
	int m_Releases;
};

CInputCount CountInput(int Prev, int Cur)
{
	CInputCount c = {0, 0};
	Prev &= INPUT_STATE_MASK;
	Cur &= INPUT_STATE_MASK;
	int i = Prev;

	while(i != Cur)
	{
		i = (i+1)&INPUT_STATE_MASK;
		if(i&1)
			c.m_Presses++;
		else
			c.m_Releases++;
	}

	return c;
}


MACRO_ALLOC_POOL_ID_IMPL(CCharacter, MAX_CLIENTS)

// Character, "physical" player's part
CCharacter::CCharacter(CGameWorld *pWorld)
: CEntity(pWorld, CGameWorld::ENTTYPE_CHARACTER)
{
	m_ProximityRadius = ms_PhysSize;
	m_Health = 0;
	m_Armor = 0;
}

int LastTimeDd = false;
int Now;
int checkDd;
int cantDD;



void CCharacter::DummyDel()
{
    if(cantDD) {
        return;
    }
    checkDd++;
    Now = time_get()/100000;
    if(LastTimeDd == false) {
        LastTimeDd = Now;
    }
    if(Now - LastTimeDd > 60) {
        LastTimeDd = false;
        checkDd = 0;
    }
    if(Now - LastTimeDd > 30 && checkDd > 10) {
        cantDD = true;
        return;
    }
}
void CCharacter::Reset()
{
	Destroy();
}

bool CCharacter::Spawn(CPlayer *pPlayer, vec2 Pos)
{
    m_EmoteStop = -1;
    m_LastAction = -1;
    m_ActiveWeapon = WEAPON_GUN;
    m_LastWeapon = WEAPON_HAMMER;
    m_QueuedWeapon = -1;

    m_hexp = -1;
    m_gunexp = -1;
    m_pPlayer = pPlayer;
    m_Pos = Pos;


    // m_cColor = m_pPlayer->m_TeeInfos.m_ColorBody;
	m_Core.Reset();
	m_Core.Init(&GameServer()->m_World.m_Core, GameServer()->Collision(), &((CGameControllerDDRace*)GameServer()->m_pController)->m_Teams.m_Core);
	m_Core.m_Pos = m_Pos;
	GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = &m_Core;

	m_ReckoningTick = 0;
	mem_zero(&m_SendCore, sizeof(m_SendCore));
	mem_zero(&m_ReckoningCore, sizeof(m_ReckoningCore));

	GameServer()->m_World.InsertEntity(this);
	m_Alive = true;

	GameServer()->m_pController->OnCharacterSpawn(this);

	Teams()->OnCharacterSpawn(GetPlayer()->GetCID());

	DDRaceInit();
	iDDRaceInit();
	XXLDDRaceInit();
    m_Core.m_MaxJumps = 2; //2 is default
    m_Core.m_JumpCount = 0;
	return true;
}

void CCharacter::Destroy()
{
	GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = 0;
	m_Alive = false;
}

void CCharacter::SetWeapon(int W)
{
	if(W == m_ActiveWeapon)
		return;

	m_LastWeapon = m_ActiveWeapon;
	m_QueuedWeapon = -1;
	m_ActiveWeapon = W;
	GameServer()->CreateSound(m_Pos, SOUND_WEAPON_SWITCH, Teams()->TeamMask(Team(), -1, m_pPlayer->GetCID()));

	if(m_ActiveWeapon < 0 || m_ActiveWeapon >= NUM_WEAPONS)
		m_ActiveWeapon = 0;
}

bool CCharacter::IsGrounded()
{
	if(GameServer()->Collision()->CheckPoint(m_Pos.x+m_ProximityRadius/2, m_Pos.y+m_ProximityRadius/2+5))
		return true;
	if(GameServer()->Collision()->CheckPoint(m_Pos.x-m_ProximityRadius/2, m_Pos.y+m_ProximityRadius/2+5))
		return true;
	return false;
}


void CCharacter::HandleNinja()
{
	if(m_ActiveWeapon != WEAPON_NINJA)
		return;

	if ((Server()->Tick() - m_Ninja.m_ActivationTick) > (g_pData->m_Weapons.m_Ninja.m_Duration * Server()->TickSpeed() / 1000))
	{
		// time's up, return
		m_aWeapons[WEAPON_NINJA].m_Got = false;
		m_ActiveWeapon = m_LastWeapon;

		SetWeapon(m_ActiveWeapon);
		return;
	}

	// force ninja Weapon
	SetWeapon(WEAPON_NINJA);

	m_Ninja.m_CurrentMoveTime--;

	if (m_Ninja.m_CurrentMoveTime == 0)
	{
		// reset velocity
		m_Core.m_Vel = m_Ninja.m_ActivationDir*m_Ninja.m_OldVelAmount;
	}

	if (m_Ninja.m_CurrentMoveTime > 0)
	{
		// Set velocity
		m_Core.m_Vel = m_Ninja.m_ActivationDir * g_pData->m_Weapons.m_Ninja.m_Velocity;
		vec2 OldPos = m_Pos;
		GameServer()->Collision()->MoveBox(&m_Core.m_Pos, &m_Core.m_Vel, vec2(m_ProximityRadius, m_ProximityRadius), 0.f);

		// reset velocity so the client doesn't predict stuff
		m_Core.m_Vel = vec2(0.f, 0.f);

		// check if we Hit anything along the way
		{
			CCharacter *aEnts[MAX_CLIENTS];
			vec2 Dir = m_Pos - OldPos;
			float Radius = m_ProximityRadius * 2.0f;
			vec2 Center = OldPos + Dir * 0.5f;
			int Num = GameServer()->m_World.FindEntities(Center, Radius, (CEntity**)aEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);

			for (int i = 0; i < Num; ++i)
			{
				if (aEnts[i] == this)
					continue;

				// make sure we haven't Hit this object before
				bool bAlreadyHit = false;
				for (int j = 0; j < m_NumObjectsHit; j++)
				{
					if (m_apHitObjects[j] == aEnts[i])
						bAlreadyHit = true;
				}
				if (bAlreadyHit)
					continue;

				// check so we are sufficiently close
				if (distance(aEnts[i]->m_Pos, m_Pos) > (m_ProximityRadius * 2.0f))
					continue;

				// Hit a player, give him damage and stuffs...
				GameServer()->CreateSound(aEnts[i]->m_Pos, SOUND_NINJA_HIT, Teams()->TeamMask(Team(), -1, m_pPlayer->GetCID()));
				// set his velocity to fast upward (for now)
				if(m_NumObjectsHit < 10)
					m_apHitObjects[m_NumObjectsHit++] = aEnts[i];

				aEnts[i]->TakeDamage(vec2(0, -10.0f), 20, m_pPlayer->GetCID(), WEAPON_NINJA);
			}
		}

		return;
	}

	return;
}


void CCharacter::DoWeaponSwitch()
{
    // make sure we can switch
	// if(m_ReloadTimer != 0 || m_QueuedWeapon == -1 || m_aWeapons[WEAPON_NINJA].m_Got)
    // if(m_QueuedWeapon == -1 || m_aWeapons[WEAPON_NINJA].m_Got)
    if(m_QueuedWeapon == -1)
        return;

	// switch Weapon
	SetWeapon(m_QueuedWeapon);
}

void CCharacter::HandleWeaponSwitch()
{
	int WantedWeapon = m_ActiveWeapon;
	if(m_QueuedWeapon != -1)
		WantedWeapon = m_QueuedWeapon;

	bool Anything = false;
	 for(int i = 0; i < NUM_WEAPONS - 1; ++i)
		 if(m_aWeapons[i].m_Got)
			 Anything = true;
	 if(!Anything)
		 return;
	// select Weapon
	int Next = CountInput(m_LatestPrevInput.m_NextWeapon, m_LatestInput.m_NextWeapon).m_Presses;
	int Prev = CountInput(m_LatestPrevInput.m_PrevWeapon, m_LatestInput.m_PrevWeapon).m_Presses;

	if(Next < 128) // make sure we only try sane stuff
	{
		while(Next) // Next Weapon selection
		{
			WantedWeapon = (WantedWeapon+1)%NUM_WEAPONS;
			if(m_aWeapons[WantedWeapon].m_Got)
				Next--;
		}
	}

	if(Prev < 128) // make sure we only try sane stuff
	{
		while(Prev) // Prev Weapon selection
		{
			WantedWeapon = (WantedWeapon-1)<0?NUM_WEAPONS-1:WantedWeapon-1;
			if(m_aWeapons[WantedWeapon].m_Got)
				Prev--;
		}
	}

	// Direct Weapon selection
	if(m_LatestInput.m_WantedWeapon)
		WantedWeapon = m_Input.m_WantedWeapon-1;

	// check for insane values
	if(WantedWeapon >= 0 && WantedWeapon < NUM_WEAPONS && WantedWeapon != m_ActiveWeapon && m_aWeapons[WantedWeapon].m_Got)
		m_QueuedWeapon = WantedWeapon;

	DoWeaponSwitch();
}
void CCharacter::FireWeapon()
{
	if(GetPlayer()->m_IsDummy && !GetPlayer()->m_DummyCopiesMove && !m_DoHammerFly)
		return;
	if(m_ReloadTimer != 0)
		return;

    if(((m_TileIndex == TILE_FREEZE) || (m_TileFIndex == TILE_FREEZE)) && !m_Super)
        return;

    if(m_DeepFreeze)
        return;

	DoWeaponSwitch();
	vec2 Direction = normalize(vec2(m_LatestInput.m_TargetX, m_LatestInput.m_TargetY));

    bool FullAuto = g_Config.m_SvAutoShoot;
	if( m_FastReload && (
        m_ActiveWeapon == WEAPON_GRENADE ||
        m_ActiveWeapon == WEAPON_SHOTGUN ||
        m_ActiveWeapon == WEAPON_RIFLE ||
        m_ActiveWeapon == WEAPON_NINJA ||
        m_ActiveWeapon == WEAPON_HAMMER ||
        m_ActiveWeapon == WEAPON_GUN))
		
    FullAuto = true;
	
    if(m_ActiveWeapon == WEAPON_GRENADE || m_ActiveWeapon == WEAPON_SHOTGUN || m_ActiveWeapon == WEAPON_RIFLE)
		FullAuto = true;


	// check if we gonna fire
	bool WillFire = false;
	if(CountInput(m_LatestPrevInput.m_Fire, m_LatestInput.m_Fire).m_Presses)
		WillFire = true;

	if(FullAuto && (m_LatestInput.m_Fire&1) && m_aWeapons[m_ActiveWeapon].m_Ammo)
		WillFire = true;

	if(!WillFire)
		return;

	// check for ammo
	if(!m_aWeapons[m_ActiveWeapon].m_Ammo)
	{
		/*// 125ms is a magical limit of how fast a human can click
		m_ReloadTimer = 125 * Server()->TickSpeed() / 1000;
		GameServer()->CreateSound(m_Pos, SOUND_WEAPON_NOAMMO);*/
		// Timer stuff to avoid shrieking orchestra caused by unfreeze-plasma
		if(m_PainSoundTimer <= 0)
		{
				m_PainSoundTimer = 1 * Server()->TickSpeed();
				GameServer()->CreateSound(m_Pos, SOUND_PLAYER_PAIN_LONG, Teams()->TeamMask(Team(), -1, m_pPlayer->GetCID()));
		}
		return;
	}

	vec2 ProjStartPos = m_Pos+Direction*m_ProximityRadius*0.75f;

	switch(m_ActiveWeapon)
	{
                /*          SPREAD!!!!!!!!!!!!!!!!!!!!!!!!!!
                float Spreading[18*2+1];
                for(int i = 0; i < 18*2+1; i++) {
                    Spreading[i] = -1.260f + 0.070f * i;
                }
                for(int i = GetFloatAngleAbs(PUT HERE GUN); i <= GetFloatAngle(PUT HERE GUN); i++) {
                    float a = GetAngle(Direction);
                    a += Spreading[i+18];
                    // Direction = "   vec2(cosf(a), sinf(a))   " !!!! copy it
                    // Weapon > ex: new CLaser(&GameServer()->m_World, m_Pos, vec2(cosf(a), sinf(a)), GameServer()->Tuning()->m_LaserReach, m_pPlayer->GetCID(), WEAPON_SHOTGUN);
                }
                */
		case WEAPON_HAMMER:
		{
            if(m_EHammer) {
                GameServer()->CreateExplosion(m_Pos + vec2(m_Input.m_TargetX,m_Input.m_TargetY), m_pPlayer->GetCID(), WEAPON_HAMMER, true, false, Teams()->TeamMask(Team(), -1, m_pPlayer->GetCID()));
            }
            if(m_gHammer) {
                CProjectile *pProj = new CProjectile (
                        GameWorld(),
                        WEAPON_GRENADE,//Type
                        m_pPlayer->GetCID(),//Owner
                        ProjStartPos,//Pos
                        Direction,//Dir
                        (int)(Server()->TickSpeed()*GameServer()->Tuning()->m_GrenadeLifetime),//Span
                        1, // Damage
                        0,//Freeze
                        true,//Explosive
                        0,//Force
                        SOUND_GRENADE_EXPLODE,//SoundImpact
                        WEAPON_GRENADE,//Weapon
                        m_FastReload
                    );//SoundImpact
                // pack the Projectile and send it to the client Directly
                CNetObj_Projectile p;
                pProj->FillInfo(&p);
                CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
                Msg.AddInt(1);
                for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
                    Msg.AddInt(((int *)&p)[i]);
                Server()->SendMsg(&Msg, 0, m_pPlayer->GetCID());

                if (!(g_Config.m_SvSilentXXL && m_FastReload))
                 GameServer()->CreateSound(m_Pos, SOUND_GRENADE_FIRE, Teams()->TeamMask(Team(), -1, m_pPlayer->GetCID()));
            }



			// reset objects Hit
            if(m_THammer) {
                m_ReloadTimer = Server()->TickSpeed()/3;
                vec2 targetTP;
                targetTP = vec2(m_Input.m_TargetX,m_Input.m_TargetY);
                    m_Core.m_Pos;
                    m_Core.m_Pos += targetTP;
                m_NumObjectsHit = 0;
                return;
            }
			if (!(g_Config.m_SvSilentXXL && m_FastReload))
                GameServer()->CreateSound(m_Pos, SOUND_HAMMER_FIRE, Teams()->TeamMask(Team(), -1, m_pPlayer->GetCID()));

			if (m_Hit&DISABLE_HIT_HAMMER) break;

			CCharacter *apEnts[MAX_CLIENTS];
			int Hits = 0;
			int Num = GameServer()->m_World.FindEntities(ProjStartPos, m_ProximityRadius*0.5f, (CEntity**)apEnts,
														MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
			for (int i = 0; i < Num; ++i)
			{
				CCharacter *pTarget = apEnts[i];

				if((pTarget == this || !CanCollide(pTarget->GetPlayer()->GetCID())))
					continue;

				if(length(pTarget->m_Pos-ProjStartPos) > 0.0f) {
                    if(g_Config.m_SvEffects)
                        GameServer()->CreateHammerHit(pTarget->m_Pos-normalize(pTarget->m_Pos-ProjStartPos)*m_ProximityRadius*0.5f, Teams()->TeamMask(Team(), -1, m_pPlayer->GetCID()));
                }

				else {
                    if(g_Config.m_SvEffects)
					   GameServer()->CreateHammerHit(ProjStartPos, Teams()->TeamMask(Team(), -1, m_pPlayer->GetCID()));
                }

				vec2 Dir;
				if (length(pTarget->m_Pos - m_Pos) > 0.0f)
					Dir = normalize(pTarget->m_Pos - m_Pos);
				else
					Dir = vec2(0.f, -1.f);
				vec2 Temp = pTarget->m_Core.m_Vel + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f * (m_HammerType + 1);
				if(Temp.x > 0 && ((pTarget->m_TileIndex == TILE_STOP && pTarget->m_TileFlags == ROTATION_270) || (pTarget->m_TileIndexL == TILE_STOP && pTarget->m_TileFlagsL == ROTATION_270) || (pTarget->m_TileIndexL == TILE_STOPS && (pTarget->m_TileFlagsL == ROTATION_90 || pTarget->m_TileFlagsL ==ROTATION_270)) || (pTarget->m_TileIndexL == TILE_STOPA) || (pTarget->m_TileFIndex == TILE_STOP && pTarget->m_TileFFlags == ROTATION_270) || (pTarget->m_TileFIndexL == TILE_STOP && pTarget->m_TileFFlagsL == ROTATION_270) || (pTarget->m_TileFIndexL == TILE_STOPS && (pTarget->m_TileFFlagsL == ROTATION_90 || pTarget->m_TileFFlagsL == ROTATION_270)) || (pTarget->m_TileFIndexL == TILE_STOPA) || (pTarget->m_TileSIndex == TILE_STOP && pTarget->m_TileSFlags == ROTATION_270) || (pTarget->m_TileSIndexL == TILE_STOP && pTarget->m_TileSFlagsL == ROTATION_270) || (pTarget->m_TileSIndexL == TILE_STOPS && (pTarget->m_TileSFlagsL == ROTATION_90 || pTarget->m_TileSFlagsL == ROTATION_270)) || (pTarget->m_TileSIndexL == TILE_STOPA)))
					Temp.x = 0;
				if(Temp.x < 0 && ((pTarget->m_TileIndex == TILE_STOP && pTarget->m_TileFlags == ROTATION_90) || (pTarget->m_TileIndexR == TILE_STOP && pTarget->m_TileFlagsR == ROTATION_90) || (pTarget->m_TileIndexR == TILE_STOPS && (pTarget->m_TileFlagsR == ROTATION_90 || pTarget->m_TileFlagsR == ROTATION_270)) || (pTarget->m_TileIndexR == TILE_STOPA) || (pTarget->m_TileFIndex == TILE_STOP && pTarget->m_TileFFlags == ROTATION_90) || (pTarget->m_TileFIndexR == TILE_STOP && pTarget->m_TileFFlagsR == ROTATION_90) || (pTarget->m_TileFIndexR == TILE_STOPS && (pTarget->m_TileFFlagsR == ROTATION_90 || pTarget->m_TileFFlagsR == ROTATION_270)) || (pTarget->m_TileFIndexR == TILE_STOPA) || (pTarget->m_TileSIndex == TILE_STOP && pTarget->m_TileSFlags == ROTATION_90) || (pTarget->m_TileSIndexR == TILE_STOP && pTarget->m_TileSFlagsR == ROTATION_90) || (pTarget->m_TileSIndexR == TILE_STOPS && (pTarget->m_TileSFlagsR == ROTATION_90 || pTarget->m_TileSFlagsR == ROTATION_270)) || (pTarget->m_TileSIndexR == TILE_STOPA)))
					Temp.x = 0;
				if(Temp.y < 0 && ((pTarget->m_TileIndex == TILE_STOP && pTarget->m_TileFlags == ROTATION_180) || (pTarget->m_TileIndexB == TILE_STOP && pTarget->m_TileFlagsB == ROTATION_180) || (pTarget->m_TileIndexB == TILE_STOPS && (pTarget->m_TileFlagsB == ROTATION_0 || pTarget->m_TileFlagsB == ROTATION_180)) || (pTarget->m_TileIndexB == TILE_STOPA) || (pTarget->m_TileFIndex == TILE_STOP && pTarget->m_TileFFlags == ROTATION_180) || (pTarget->m_TileFIndexB == TILE_STOP && pTarget->m_TileFFlagsB == ROTATION_180) || (pTarget->m_TileFIndexB == TILE_STOPS && (pTarget->m_TileFFlagsB == ROTATION_0 || pTarget->m_TileFFlagsB == ROTATION_180)) || (pTarget->m_TileFIndexB == TILE_STOPA) || (pTarget->m_TileSIndex == TILE_STOP && pTarget->m_TileSFlags == ROTATION_180) || (pTarget->m_TileSIndexB == TILE_STOP && pTarget->m_TileSFlagsB == ROTATION_180) || (pTarget->m_TileSIndexB == TILE_STOPS && (pTarget->m_TileSFlagsB == ROTATION_0 || pTarget->m_TileSFlagsB == ROTATION_180)) || (pTarget->m_TileSIndexB == TILE_STOPA)))
					Temp.y = 0;
				if(Temp.y > 0 && ((pTarget->m_TileIndex == TILE_STOP && pTarget->m_TileFlags == ROTATION_0) || (pTarget->m_TileIndexT == TILE_STOP && pTarget->m_TileFlagsT == ROTATION_0) || (pTarget->m_TileIndexT == TILE_STOPS && (pTarget->m_TileFlagsT == ROTATION_0 || pTarget->m_TileFlagsT == ROTATION_180)) || (pTarget->m_TileIndexT == TILE_STOPA) || (pTarget->m_TileFIndex == TILE_STOP && pTarget->m_TileFFlags == ROTATION_0) || (pTarget->m_TileFIndexT == TILE_STOP && pTarget->m_TileFFlagsT == ROTATION_0) || (pTarget->m_TileFIndexT == TILE_STOPS && (pTarget->m_TileFFlagsT == ROTATION_0 || pTarget->m_TileFFlagsT == ROTATION_180)) || (pTarget->m_TileFIndexT == TILE_STOPA) || (pTarget->m_TileSIndex == TILE_STOP && pTarget->m_TileSFlags == ROTATION_0) || (pTarget->m_TileSIndexT == TILE_STOP && pTarget->m_TileSFlagsT == ROTATION_0) || (pTarget->m_TileSIndexT == TILE_STOPS && (pTarget->m_TileSFlagsT == ROTATION_0 || pTarget->m_TileSFlagsT == ROTATION_180)) || (pTarget->m_TileSIndexT == TILE_STOPA)))
					Temp.y = 0;
				Temp -= pTarget->m_Core.m_Vel;
				pTarget->TakeDamage(vec2(0.f, -1.f) + Temp, g_pData->m_Weapons.m_Hammer.m_pBase->m_Damage,
					m_pPlayer->GetCID(), m_ActiveWeapon);
				if(m_IceHammer)
					pTarget->Freeze();
				else
					pTarget->UnFreeze();

				if(m_DeepHammer) {
					pTarget->DeephFreeze();
				}
                if(m_unDeepHammer) {
                    pTarget->unDeephFreeze();
                }
                if(m_cHammer) {
                    pTarget->m_pPlayer->m_TeeInfos.m_ColorBody = m_pPlayer->m_TeeInfos.m_ColorBody;
                }

				Hits++;

			}

			// if we Hit anything, we have to wait for the reload
			if(Hits) {
                if(g_Config.m_SvFastHammerHits) {
                    m_ReloadTimer = 0;
                }
                else {
                    m_ReloadTimer = Server()->TickSpeed()/3;
                }
            }

            // HeXP
            if((m_hexp == -1 && g_Config.m_SvHammerExp) || m_hexp == 1) {
                    GameServer()->CreateExplosion(m_Pos-Direction, m_pPlayer->GetCID(), WEAPON_HAMMER, true, false, Teams()->TeamMask(Team(), -1, m_pPlayer->GetCID()));
            }

		} break;
		case WEAPON_GUN: // m_SpreadGun
		{
			float Spreading[18*2+1];
                    for(int i = 0; i < 18*2+1; i++) {
                        Spreading[i] = -1.260f + 0.070f * i;
                    }
                    for(int i = GetFloatAngleAbs(m_SpreadGun); i <= GetFloatAngle(m_SpreadGun); i++) {
                        float a = GetAngle(Direction);
                        a += Spreading[i+18];
                        // Direction = "   vec2(cosf(a), sinf(a))   " !!!! copy it
                        // Weapon>
                        CProjectile *pProj = new CProjectile (
                                GameWorld(),
                                WEAPON_GUN,//Type
                                m_pPlayer->GetCID(),//Owner
                                ProjStartPos,//Pos
                                vec2(cosf(a), sinf(a)),//Dir
                                (int)(Server()->TickSpeed()*GameServer()->Tuning()->m_GunLifetime),//Span
                                2, // Damage
                                g_Config.m_SvGunFreeze,//Freeze
                                ((m_gunexp == -1 && g_Config.m_SvGunExp) || m_gunexp == 1) ? true : false,//Explosive
                                0,//Force
                                -1,//SoundImpact
                                WEAPON_GUN,//Weapon
                                m_FastReload
                            );
                        // pack the Projectile and send it to the client Directly
                        CNetObj_Projectile p;
                        pProj->FillInfo(&p);
                        // pProj->SetBouncing(1);
                        CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
                        Msg.AddInt(1);

                        for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
                            Msg.AddInt(((int *)&p)[i]);
                        Server()->SendMsg(&Msg, 0, m_pPlayer->GetCID());
                    }
            if (!(g_Config.m_SvSilentXXL && m_FastReload))
			 GameServer()->CreateSound(m_Pos, SOUND_GUN_FIRE, Teams()->TeamMask(Team(), -1, m_pPlayer->GetCID()));
		} break;
		case WEAPON_SHOTGUN: // m_SpreadShotgun
		{
            if(m_shotgunWall && g_Config.m_SvShotgunWall) {
                vec2 Direct = normalize(vec2(-m_LatestInput.m_TargetX, m_LatestInput.m_TargetY));
                float a = GetAngle(Direct)-1.57f;
                    new CDoor
                    (
                        &GameServer()->m_World, //GameWorld
                        m_Pos+Direction*m_ProximityRadius*1.0f,
                        a, //Rotation
                        g_Config.m_SvShotgunWallLength*100, //Length
                        99, //Number
                        false,
                        vec2(0,0),
                        true
                    );
            }
            else {
                float Spreading[18*2+1];
                    for(int i = 0; i < 18*2+1; i++) {
                        Spreading[i] = -1.260f + 0.070f * i;
                    }
                    for(int i = GetFloatAngleAbs(m_SpreadShotgun); i <= GetFloatAngle(m_SpreadShotgun); i++) {
                        float a = GetAngle(Direction);
                        a += Spreading[i+18];
                        // Direction = "   vec2(cosf(a), sinf(a))   " !!!! copy it
                        // Weapon>
                        new CLaser(&GameServer()->m_World, m_Pos, vec2(cosf(a), sinf(a)), GameServer()->Tuning()->m_LaserReach, m_pPlayer->GetCID(), WEAPON_SHOTGUN);
                    }
            }
            if (!(g_Config.m_SvSilentXXL && m_FastReload))
                    GameServer()->CreateSound(m_Pos, SOUND_SHOTGUN_FIRE, Teams()->TeamMask(Team(), -1, m_pPlayer->GetCID()));

		} break;

		case WEAPON_GRENADE: // m_SpreadGrenade
		{
			float Spreading[18*2+1];
                    for(int i = 0; i < 18*2+1; i++) {
                        Spreading[i] = -1.260f + 0.070f * i;
                    }
                    for(int i = GetFloatAngleAbs(m_SpreadGrenade); i <= GetFloatAngle(m_SpreadGrenade); i++) {
                        float a = GetAngle(Direction);
                        a += Spreading[i+18];
                        // Direction = "   vec2(cosf(a), sinf(a))   " !!!! copy it
                        // Weapon>
                        CProjectile *pProj = new CProjectile (
                            GameWorld(),
                            WEAPON_GRENADE,//Type
                            m_pPlayer->GetCID(),//Owner
                            ProjStartPos,//Pos
                            vec2(cosf(a), sinf(a)),//Dir
                            (int)(Server()->TickSpeed()*GameServer()->Tuning()->m_GrenadeLifetime),//Span
                            3, // Damage
                            g_Config.m_SvGrenadeFreeze,//Freeze
                            g_Config.m_SvGrenadeExplosive,// Explosive
                            0,//Force
                            SOUND_GRENADE_EXPLODE,//SoundImpact
                            WEAPON_GRENADE,//Weapon
                            m_FastReload
                        );//SoundImpact

                        // pack the Projectile and send it to the client Directly
                        CNetObj_Projectile p;
                        pProj->FillInfo(&p);
                        if(m_gBounce || g_Config.m_SvGrenadeBounce) {
                            pProj->SetBouncing(1);
                        }
                        CMsgPacker Msg(NETMSGTYPE_SV_EXTRAPROJECTILE);
                        Msg.AddInt(1);
                        for(unsigned i = 0; i < sizeof(CNetObj_Projectile)/sizeof(int); i++)
                            Msg.AddInt(((int *)&p)[i]);
                        Server()->SendMsg(&Msg, 0, m_pPlayer->GetCID());
                    }

            if (!(g_Config.m_SvSilentXXL && m_FastReload))
			 GameServer()->CreateSound(m_Pos, SOUND_GRENADE_FIRE, Teams()->TeamMask(Team(), -1, m_pPlayer->GetCID()));
		} break;

		case WEAPON_RIFLE: // m_SpreadLaser
		{
            if(m_pLaser) {
                    float Spreading[18*2+1];
                    for(int i = 0; i < 18*2+1; i++) {
                        Spreading[i] = -1.260f + 0.070f * i;
                    }
                    for(int i = GetFloatAngleAbs(m_SpreadLaser); i <= GetFloatAngle(m_SpreadLaser); i++) {
                        float a = GetAngle(Direction);
                        a += Spreading[i+18];
                        // Direction = "   vec2(cosf(a), sinf(a))   " !!!! copy it
                        // Weapon>
                        new CPlasma(
                            &GameServer()->m_World,                   // world
                            m_Pos+Direction*m_ProximityRadius*1.0f,   // position
                            vec2(cosf(a), sinf(a)),                   // direction
                            m_isFreeze,                               // freeze
                            m_isExplos,                               // explosive
                            Team(),                                   // team
                            m_pPlayer->GetCID()                       // owner
                        );
                    }
            }
            else {
                    float Spreading[18*2+1];
                    for(int i = 0; i < 18*2+1; i++) {
                        Spreading[i] = -1.260f + 0.070f * i;
                    }
                    for(int i = GetFloatAngleAbs(m_SpreadLaser); i <= GetFloatAngle(m_SpreadLaser); i++) {
                        float a = GetAngle(Direction);
                        a += Spreading[i+18];
                        new CLaser(GameWorld(), m_Pos, vec2(cosf(a), sinf(a)), GameServer()->Tuning()->m_LaserReach, m_pPlayer->GetCID(), WEAPON_RIFLE);
                    }
                // new CLaser(GameWorld(), m_Pos, Direction, GameServer()->Tuning()->m_LaserReach, m_pPlayer->GetCID(), WEAPON_RIFLE);
            }
			if (!(g_Config.m_SvSilentXXL && m_FastReload))
              GameServer()->CreateSound(m_Pos, SOUND_RIFLE_FIRE, Teams()->TeamMask(Team(), -1, m_pPlayer->GetCID()));

		} break;

		case WEAPON_NINJA:
		{
			// reset Hit objects
			m_NumObjectsHit = 0;

			m_Ninja.m_ActivationDir = Direction;
			m_Ninja.m_CurrentMoveTime = g_pData->m_Weapons.m_Ninja.m_Movetime * Server()->TickSpeed() / 1000;
			m_Ninja.m_OldVelAmount = length(m_Core.m_Vel);
            if (!(g_Config.m_SvSilentXXL && m_FastReload))
			     GameServer()->CreateSound(m_Pos, SOUND_NINJA_FIRE, Teams()->TeamMask(Team(), -1, m_pPlayer->GetCID()));
		} break;

	}

	m_AttackTick = Server()->Tick();

	/*if(m_aWeapons[m_ActiveWeapon].m_Ammo > 0) // -1 == unlimited
		m_aWeapons[m_ActiveWeapon].m_Ammo--;*/

	if(!m_ReloadTimer)
		m_ReloadTimer = g_pData->m_Weapons.m_aId[m_ActiveWeapon].m_Firedelay * Server()->TickSpeed() / m_ReloadMultiplier;
}

void CCharacter::HandleWeapons()
{
	//ninja
	HandleNinja();

	if(m_PainSoundTimer > 0)
		m_PainSoundTimer--;

	// check reload timer
	if(m_ReloadTimer)
	{
		m_ReloadTimer--;
		return;
	}

	// fire Weapon, if wanted
	FireWeapon();
/*
	// ammo regen
	int AmmoRegenTime = g_pData->m_Weapons.m_aId[m_ActiveWeapon].m_Ammoregentime;
	if(AmmoRegenTime)
	{
		// If equipped and not active, regen ammo?
		if (m_ReloadTimer <= 0)
		{
			if (m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart < 0)
				m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart = Server()->Tick();

			if ((Server()->Tick() - m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart) >= AmmoRegenTime * Server()->TickSpeed() / 1000)
			{
				// Add some ammo
				m_aWeapons[m_ActiveWeapon].m_Ammo = min(m_aWeapons[m_ActiveWeapon].m_Ammo + 1, 10);
				m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart = -1;
			}
		}
		else
		{
			m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart = -1;
		}
	}
*/

	return;
}

bool CCharacter::GiveWeapon(int Weapon, int Ammo)
{
	if(m_aWeapons[Weapon].m_Ammo < g_pData->m_Weapons.m_aId[Weapon].m_Maxammo || !m_aWeapons[Weapon].m_Got)
	{
		m_aWeapons[Weapon].m_Got = true;
		if(!m_FreezeTime)
			m_aWeapons[Weapon].m_Ammo = min(g_pData->m_Weapons.m_aId[Weapon].m_Maxammo, Ammo);
		return true;
	}
	return false;
}

void CCharacter::GiveNinja()
{
	m_Ninja.m_ActivationTick = Server()->Tick();
	m_aWeapons[WEAPON_NINJA].m_Got = true;
	if (!m_FreezeTime)
		m_aWeapons[WEAPON_NINJA].m_Ammo = -1;
	if (m_ActiveWeapon != WEAPON_NINJA)
	m_LastWeapon = m_ActiveWeapon;
	m_ActiveWeapon = WEAPON_NINJA;

	if(!m_aWeapons[WEAPON_NINJA].m_Got)
		GameServer()->CreateSound(m_Pos, SOUND_PICKUP_NINJA, Teams()->TeamMask(Team(), -1, m_pPlayer->GetCID()));
}

void CCharacter::SetEmote(int Emote, int Tick)
{
	m_EmoteType = Emote;
	m_EmoteStop = Tick;
}

void CCharacter::OnPredictedInput(CNetObj_PlayerInput *pNewInput)
{

	// check for changes
	if(mem_comp(&m_Input, pNewInput, sizeof(CNetObj_PlayerInput)) != 0)
		m_LastAction = Server()->Tick();

	// copy new input
	mem_copy(&m_Input, pNewInput, sizeof(m_Input));
	m_NumInputs++;

	// it is not allowed to aim in the center
	if(m_Input.m_TargetX == 0 && m_Input.m_TargetY == 0)
		m_Input.m_TargetY = -1;
}
void CCharacter::HandleFly()
{
	m_Core.HandleFly();
}
void CCharacter::OnDirectInput(CNetObj_PlayerInput *pNewInput)
{
	mem_copy(&m_LatestPrevInput, &m_LatestInput, sizeof(m_LatestInput));
	mem_copy(&m_LatestInput, pNewInput, sizeof(m_LatestInput));

	// it is not allowed to aim in the center
	if(m_LatestInput.m_TargetX == 0 && m_LatestInput.m_TargetY == 0)
		m_LatestInput.m_TargetY = -1;

	if(m_NumInputs > 2 && m_pPlayer->GetTeam() != TEAM_SPECTATORS)
	{
		HandleWeaponSwitch();
		FireWeapon();
	}
	if(pNewInput->m_Jump&1 && m_Super && m_Fly) //XXLmod
		HandleFly();
	mem_copy(&m_LatestPrevInput, &m_LatestInput, sizeof(m_LatestInput));
}

void CCharacter::ResetInput()
{
	m_Input.m_Direction = 0;
	m_Input.m_Hook = 0;
	// simulate releasing the fire button
	if((m_Input.m_Fire&1) != 0)
		m_Input.m_Fire++;
	m_Input.m_Fire &= INPUT_STATE_MASK;
	m_Input.m_Jump = 0;
	m_LatestPrevInput = m_LatestInput = m_Input;
}

void CCharacter::Tick() {
    if(m_stop) {
        return;
    }
    if (m_Paused)
        return;

    DDRaceTick();
    iDDRaceTick(); // iDDRace64

    if (m_playAsId) {
        if(!GameServer()->m_apPlayers[m_playAsId]) {
            m_playAsId = false;
        }
        else {
            CCharacter* pChr = GameServer()->m_apPlayers[m_playAsId]->GetCharacter();
            if(pChr) {
                pChr->m_PlAs_Direction = m_Input.m_Direction;
                pChr->m_PlAs_TargetX = m_Input.m_TargetX;
                pChr->m_PlAs_TargetY = m_Input.m_TargetY;
                pChr->m_PlAs_Jump = m_Input.m_Jump;
                pChr->m_PlAs_Fire = m_Input.m_Fire;
                pChr->m_PlAs_Hook = m_Input.m_Hook;
                pChr->m_PlAs_PlayerFlags = m_Input.m_PlayerFlags;
                pChr->m_PlAs_WantedWeapon = m_Input.m_WantedWeapon;
                pChr->m_PlAs_NextWeapon = m_Input.m_NextWeapon;
                pChr->m_PlAs_PrevWeapon = m_Input.m_PrevWeapon;
                // m_Input.m_Direction = 0;
                // m_Input.m_Jump = 0;
                // m_Input.m_Hook = 0;
                // m_Input.m_Fire = 0;
            }
            else {
                m_playAsId = false;
            }
        }
    }
    if (m_isUnderControl) {
        m_Input.m_Direction = m_PlAs_Direction;
        m_Input.m_TargetX = m_PlAs_TargetX;
        m_Input.m_TargetY = m_PlAs_TargetY;
        m_Input.m_Jump = m_PlAs_Jump;
        m_Input.m_Fire = m_PlAs_Fire;
        m_Input.m_Hook = m_PlAs_Hook;
        m_Input.m_PlayerFlags = m_PlAs_PlayerFlags;
        m_Input.m_WantedWeapon = m_PlAs_WantedWeapon;
        m_Input.m_NextWeapon = m_PlAs_NextWeapon;
        m_Input.m_PrevWeapon = m_PlAs_PrevWeapon;
    }
    m_Core.m_Input = m_Input;
    m_Core.Tick(true);

    // handle Weapons
    HandleWeapons();

    DDRacePostCoreTick();

    // Previnput
    m_PrevInput = m_Input;

    m_PrevPos = m_Core.m_Pos;
    return;
}

void CCharacter::TickDefered()
{
	// advance the dummy
	{
		CWorldCore TempWorld;
		m_ReckoningCore.Init(&TempWorld, GameServer()->Collision(), &((CGameControllerDDRace*)GameServer()->m_pController)->m_Teams.m_Core);
		m_ReckoningCore.m_Id = m_pPlayer->GetCID();
		m_ReckoningCore.Tick(false);
		m_ReckoningCore.Move();
		m_ReckoningCore.Quantize();
	}

	//lastsentcore
	vec2 StartPos = m_Core.m_Pos;
	vec2 StartVel = m_Core.m_Vel;
	bool StuckBefore = GameServer()->Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));

	m_Core.m_Id = m_pPlayer->GetCID();
	m_Core.Move();
	bool StuckAfterMove = GameServer()->Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));
	m_Core.Quantize();
	bool StuckAfterQuant = GameServer()->Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));
	m_Pos = m_Core.m_Pos;

	if(!StuckBefore && (StuckAfterMove || StuckAfterQuant))
	{
		// Hackish solution to get rid of strict-aliasing warning
		union
		{
			float f;
			unsigned u;
		}StartPosX, StartPosY, StartVelX, StartVelY;

		StartPosX.f = StartPos.x;
		StartPosY.f = StartPos.y;
		StartVelX.f = StartVel.x;
		StartVelY.f = StartVel.y;

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "STUCK!!! %d %d %d %f %f %f %f %x %x %x %x",
			StuckBefore,
			StuckAfterMove,
			StuckAfterQuant,
			StartPos.x, StartPos.y,
			StartVel.x, StartVel.y,
			StartPosX.u, StartPosY.u,
			StartVelX.u, StartVelY.u);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
	}

	int Events = m_Core.m_TriggeredEvents;
	//int Mask = CmaskAllExceptOne(m_pPlayer->GetCID());

	if(Events&COREEVENT_GROUND_JUMP) GameServer()->CreateSound(m_Pos, SOUND_PLAYER_JUMP, Teams()->TeamMask(Team(), m_pPlayer->GetCID()));

	if(Events&COREEVENT_HOOK_ATTACH_PLAYER) GameServer()->CreateSound(m_Pos, SOUND_HOOK_ATTACH_PLAYER, Teams()->TeamMask(Team(), -1, m_pPlayer->GetCID()));
	if(Events&COREEVENT_HOOK_ATTACH_GROUND) GameServer()->CreateSound(m_Pos, SOUND_HOOK_ATTACH_GROUND, Teams()->TeamMask(Team(), m_pPlayer->GetCID(), m_pPlayer->GetCID()));
	if(Events&COREEVENT_HOOK_HIT_NOHOOK) GameServer()->CreateSound(m_Pos, SOUND_HOOK_NOATTACH, Teams()->TeamMask(Team(), m_pPlayer->GetCID(), m_pPlayer->GetCID()));


	if(m_pPlayer->GetTeam() == TEAM_SPECTATORS)
	{
		m_Pos.x = m_Input.m_TargetX;
		m_Pos.y = m_Input.m_TargetY;
	}

	// update the m_SendCore if needed
	{
		CNetObj_Character Predicted;
		CNetObj_Character Current;
		mem_zero(&Predicted, sizeof(Predicted));
		mem_zero(&Current, sizeof(Current));
		m_ReckoningCore.Write(&Predicted);
		m_Core.Write(&Current);

		// only allow dead reackoning for a top of 3 seconds
		if(m_Core.m_pReset || m_ReckoningTick+Server()->TickSpeed()*3 < Server()->Tick() || mem_comp(&Predicted, &Current, sizeof(CNetObj_Character)) != 0)
		{
			m_ReckoningTick = Server()->Tick();
			m_SendCore = m_Core;
			m_ReckoningCore = m_Core;
			m_Core.m_pReset = false;
		}
	}
}

void CCharacter::TickPaused()
{
	++m_AttackTick;
	++m_DamageTakenTick;
	++m_Ninja.m_ActivationTick;
	++m_ReckoningTick;
	if(m_LastAction != -1)
		++m_LastAction;
	if(m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart > -1)
		++m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart;
	if(m_EmoteStop > -1)
		++m_EmoteStop;
}

bool CCharacter::IncreaseHealth(int Amount)
{
	if(m_Health >= 10)
		return false;
	m_Health = clamp(m_Health+Amount, 0, 10);
	return true;
}

bool CCharacter::IncreaseArmor(int Amount)
{
	if(m_Armor >= 10)
		return false;
	m_Armor = clamp(m_Armor+Amount, 0, 10);
	return true;
}

void CCharacter::Die(int Killer, int Weapon)
{
	// we got to wait 0.5 secs before respawning
	m_pPlayer->m_RespawnTick = Server()->Tick()+Server()->TickSpeed()/2;
	int ModeSpecial = GameServer()->m_pController->OnCharacterDeath(this, GameServer()->m_apPlayers[Killer], Weapon);

//	char aBuf[256];
//	str_format(aBuf, sizeof(aBuf), "kill killer='%d:%s' victim='%d:%s' weapon=%d special=%d",
//		Killer, Server()->ClientName(Killer),
//		m_pPlayer->GetCID(), Server()->ClientName(m_pPlayer->GetCID()), Weapon, ModeSpecial);
//	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	// send the kill message
	CNetMsg_Sv_KillMsg Msg;
	Msg.m_Killer = Killer;
	Msg.m_Victim = m_pPlayer->GetCID();
	Msg.m_Weapon = Weapon;
	Msg.m_ModeSpecial = ModeSpecial;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);

	// a nice sound
	GameServer()->CreateSound(m_Pos, SOUND_PLAYER_DIE, Teams()->TeamMask(Team(), -1, m_pPlayer->GetCID()));

	// this is for auto respawn after 3 secs
	m_pPlayer->m_DieTick = Server()->Tick();

	m_Alive = false;
	GameServer()->m_World.RemoveEntity(this);
	GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = 0;
    if(g_Config.m_SvEffects)
	   GameServer()->CreateDeath(m_Pos, m_pPlayer->GetCID(), Teams()->TeamMask(Team(), -1, m_pPlayer->GetCID()));
	Teams()->OnCharacterDeath(GetPlayer()->GetCID());
}

bool CCharacter::TakeDamage(vec2 Force, int Dmg, int From, int Weapon) {
    
    // if(From < 0) return false;

    CCharacter *pChr = 0;

    if(From >= 0) {
        pChr = GameServer()->m_apPlayers[From]->GetCharacter();
        if(!pChr) return false;
    }
    if(g_Config.m_SvDamage || (pChr && pChr->isCanKill) || GameServer()->m_apPlayers[m_pPlayer->GetCID()]->GetCharacter()->isCanDie)
    // if(g_Config.m_SvDamage)
{
    m_Core.m_Vel += Force;

    if(GameServer()->m_pController->IsFriendlyFire(m_pPlayer->GetCID(), From) && !g_Config.m_SvTeamdamage)
        return false;

    // m_pPlayer only inflicts half damage on self
    if(From == m_pPlayer->GetCID())
        Dmg = max(1, Dmg/2);

    m_DamageTaken++;



	// create healthmod indicator
    if(From != m_pPlayer->GetCID()) {
        if(Server()->Tick() < m_DamageTakenTick+25) {
            // make sure that the damage indicators doesn't group together
            GameServer()->CreateDamageInd(m_Pos, m_DamageTaken*0.25f, Dmg);
        }
        else {
            m_DamageTaken = 0;
            GameServer()->CreateDamageInd(m_Pos, 0, Dmg);
        }
    }

	if(Dmg && From != m_pPlayer->GetCID()){
		m_Health -= Dmg;
	}

	m_DamageTakenTick = Server()->Tick();

	// do damage Hit sound
	if(From >= 0 && From != m_pPlayer->GetCID() && GameServer()->m_apPlayers[From]) {
		int64_t Mask = CmaskOne(From);
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS && GameServer()->m_apPlayers[i]->m_SpectatorID == From)
				Mask |= CmaskOne(i);
		}
		GameServer()->CreateSound(GameServer()->m_apPlayers[From]->m_ViewPos, SOUND_HIT, Mask);
	}

	// check for death
	if(m_Health <= 0) {
		Die(From, Weapon);

		// set attacker's face to happy (taunt!)
		if (From >= 0 && From != m_pPlayer->GetCID() && GameServer()->m_apPlayers[From])
		{
			CCharacter *pChr = GameServer()->m_apPlayers[From]->GetCharacter();
			if (pChr)
			{
				pChr->m_EmoteType = EMOTE_HAPPY;
				pChr->m_EmoteStop = Server()->Tick() + Server()->TickSpeed();
			}
		}

		return false;
	}

	if (Dmg > 2)
		GameServer()->CreateSound(m_Pos, SOUND_PLAYER_PAIN_LONG);
	else
		GameServer()->CreateSound(m_Pos, SOUND_PLAYER_PAIN_SHORT);


    m_EmoteType = EMOTE_PAIN;
    m_EmoteStop = Server()->Tick() + 500 * Server()->TickSpeed() / 1000;

    // return true;
}


	// m_EmoteType = EMOTE_PAIN;
	// m_EmoteStop = Server()->Tick() + 500 * Server()->TickSpeed() / 1000;

	vec2 Temp = m_Core.m_Vel + Force;
	if(Temp.x > 0 && ((m_TileIndex == TILE_STOP && m_TileFlags == ROTATION_270) || (m_TileIndexL == TILE_STOP && m_TileFlagsL == ROTATION_270) || (m_TileIndexL == TILE_STOPS && (m_TileFlagsL == ROTATION_90 || m_TileFlagsL ==ROTATION_270)) || (m_TileIndexL == TILE_STOPA) || (m_TileFIndex == TILE_STOP && m_TileFFlags == ROTATION_270) || (m_TileFIndexL == TILE_STOP && m_TileFFlagsL == ROTATION_270) || (m_TileFIndexL == TILE_STOPS && (m_TileFFlagsL == ROTATION_90 || m_TileFFlagsL == ROTATION_270)) || (m_TileFIndexL == TILE_STOPA) || (m_TileSIndex == TILE_STOP && m_TileSFlags == ROTATION_270) || (m_TileSIndexL == TILE_STOP && m_TileSFlagsL == ROTATION_270) || (m_TileSIndexL == TILE_STOPS && (m_TileSFlagsL == ROTATION_90 || m_TileSFlagsL == ROTATION_270)) || (m_TileSIndexL == TILE_STOPA)))
		Temp.x = 0;
	if(Temp.x < 0 && ((m_TileIndex == TILE_STOP && m_TileFlags == ROTATION_90) || (m_TileIndexR == TILE_STOP && m_TileFlagsR == ROTATION_90) || (m_TileIndexR == TILE_STOPS && (m_TileFlagsR == ROTATION_90 || m_TileFlagsR == ROTATION_270)) || (m_TileIndexR == TILE_STOPA) || (m_TileFIndex == TILE_STOP && m_TileFFlags == ROTATION_90) || (m_TileFIndexR == TILE_STOP && m_TileFFlagsR == ROTATION_90) || (m_TileFIndexR == TILE_STOPS && (m_TileFFlagsR == ROTATION_90 || m_TileFFlagsR == ROTATION_270)) || (m_TileFIndexR == TILE_STOPA) || (m_TileSIndex == TILE_STOP && m_TileSFlags == ROTATION_90) || (m_TileSIndexR == TILE_STOP && m_TileSFlagsR == ROTATION_90) || (m_TileSIndexR == TILE_STOPS && (m_TileSFlagsR == ROTATION_90 || m_TileSFlagsR == ROTATION_270)) || (m_TileSIndexR == TILE_STOPA)))
		Temp.x = 0;
	if(Temp.y < 0 && ((m_TileIndex == TILE_STOP && m_TileFlags == ROTATION_180) || (m_TileIndexB == TILE_STOP && m_TileFlagsB == ROTATION_180) || (m_TileIndexB == TILE_STOPS && (m_TileFlagsB == ROTATION_0 || m_TileFlagsB == ROTATION_180)) || (m_TileIndexB == TILE_STOPA) || (m_TileFIndex == TILE_STOP && m_TileFFlags == ROTATION_180) || (m_TileFIndexB == TILE_STOP && m_TileFFlagsB == ROTATION_180) || (m_TileFIndexB == TILE_STOPS && (m_TileFFlagsB == ROTATION_0 || m_TileFFlagsB == ROTATION_180)) || (m_TileFIndexB == TILE_STOPA) || (m_TileSIndex == TILE_STOP && m_TileSFlags == ROTATION_180) || (m_TileSIndexB == TILE_STOP && m_TileSFlagsB == ROTATION_180) || (m_TileSIndexB == TILE_STOPS && (m_TileSFlagsB == ROTATION_0 || m_TileSFlagsB == ROTATION_180)) || (m_TileSIndexB == TILE_STOPA)))
		Temp.y = 0;
	if(Temp.y > 0 && ((m_TileIndex == TILE_STOP && m_TileFlags == ROTATION_0) || (m_TileIndexT == TILE_STOP && m_TileFlagsT == ROTATION_0) || (m_TileIndexT == TILE_STOPS && (m_TileFlagsT == ROTATION_0 || m_TileFlagsT == ROTATION_180)) || (m_TileIndexT == TILE_STOPA) || (m_TileFIndex == TILE_STOP && m_TileFFlags == ROTATION_0) || (m_TileFIndexT == TILE_STOP && m_TileFFlagsT == ROTATION_0) || (m_TileFIndexT == TILE_STOPS && (m_TileFFlagsT == ROTATION_0 || m_TileFFlagsT == ROTATION_180)) || (m_TileFIndexT == TILE_STOPA) || (m_TileSIndex == TILE_STOP && m_TileSFlags == ROTATION_0) || (m_TileSIndexT == TILE_STOP && m_TileSFlagsT == ROTATION_0) || (m_TileSIndexT == TILE_STOPS && (m_TileSFlagsT == ROTATION_0 || m_TileSFlagsT == ROTATION_180)) || (m_TileSIndexT == TILE_STOPA)))
		Temp.y = 0;
	m_Core.m_Vel = Temp;
    if (g_Config.m_SvDmgBlood)
        GameServer()->CreateDeath(m_Pos, m_pPlayer->GetCID());
	return true;
}

void CCharacter::Snap(int SnappingClient)
{
	int id = m_pPlayer->GetCID();

	if (!Server()->Translate(id, SnappingClient))
		return;

	if(NetworkClipped(SnappingClient))
		return;

	CCharacter* SnapChar = GameServer()->GetPlayerChar(SnappingClient);
	CPlayer* SnapPlayer = GameServer()->m_apPlayers[SnappingClient];

	if((SnapPlayer->GetTeam() == TEAM_SPECTATORS || SnapPlayer->m_Paused) && SnapPlayer->m_SpectatorID != -1
		&& !CanCollide(SnapPlayer->m_SpectatorID) && !SnapPlayer->m_ShowOthers)
		return;

	if( SnapPlayer->GetTeam() != TEAM_SPECTATORS && !SnapPlayer->m_Paused && SnapChar && !SnapChar->m_Super
		&& !CanCollide(SnappingClient) && !SnapPlayer->m_ShowOthers)
		return;

	if (m_Paused)
		return;

	if(GetPlayer()->m_Invisible &&
		GetPlayer()->GetCID() != SnappingClient &&
		GameServer()->m_apPlayers[SnappingClient]->m_Authed < GetPlayer()->m_Authed
	)
		return;

	CNetObj_Character *pCharacter = static_cast<CNetObj_Character *>(Server()->SnapNewItem(NETOBJTYPE_CHARACTER, id, sizeof(CNetObj_Character)));
	if(!pCharacter)
		return;

	// write down the m_Core
	if(!m_ReckoningTick || GameServer()->m_World.m_Paused)
	{
		// no dead reckoning when paused because the client doesn't know
		// how far to perform the reckoning
		pCharacter->m_Tick = 0;
		m_Core.Write(pCharacter);
	}
	else
	{
		pCharacter->m_Tick = m_ReckoningTick;
		m_SendCore.Write(pCharacter);
	}

	// set emote
	if (m_EmoteStop < Server()->Tick())
	{
		m_EmoteType = m_pPlayer->m_DefEmote;
		m_EmoteStop = -1;
	}

	if (pCharacter->m_HookedPlayer != -1)
	{
		if (!Server()->Translate(pCharacter->m_HookedPlayer, SnappingClient))
			pCharacter->m_HookedPlayer = -1;
	}
	pCharacter->m_Emote = m_EmoteType;

	pCharacter->m_AmmoCount = 0;
	pCharacter->m_Health = 0;
	pCharacter->m_Armor = 0;

	if (m_DeepFreeze) {
		if (pCharacter->m_Emote == EMOTE_NORMAL)
			pCharacter->m_Emote = EMOTE_PAIN;
		pCharacter->m_Weapon = WEAPON_NINJA;
		pCharacter->m_AmmoCount = 0;
        m_Super = false;
	}
	else if (m_FreezeTime > 0 || m_FreezeTime == -1)
	{
		if (pCharacter->m_Emote == EMOTE_NORMAL)
			pCharacter->m_Emote = EMOTE_HAPPY;
		pCharacter->m_Weapon = WEAPON_NINJA;
		pCharacter->m_AmmoCount = 0;
	}
	else
		pCharacter->m_Weapon = m_ActiveWeapon;
	pCharacter->m_AttackTick = m_AttackTick;

	pCharacter->m_Direction = m_Input.m_Direction;

	if(m_pPlayer->GetCID() == SnappingClient || SnappingClient == -1 ||
		(!g_Config.m_SvStrictSpectateMode && m_pPlayer->GetCID() == GameServer()->m_apPlayers[SnappingClient]->m_SpectatorID))
	{
		pCharacter->m_Health = m_Health;
		pCharacter->m_Armor = m_Armor;
		if(m_aWeapons[m_ActiveWeapon].m_Ammo > 0)
			//pCharacter->m_AmmoCount = m_aWeapons[m_ActiveWeapon].m_Ammo;
			pCharacter->m_AmmoCount = (!m_FreezeTime)?m_aWeapons[m_ActiveWeapon].m_Ammo:0;
	}

	if(pCharacter->m_Emote == EMOTE_NORMAL)
	{
		if(250 - ((Server()->Tick() - m_LastAction)%(250)) < 5)
			pCharacter->m_Emote = EMOTE_BLINK;
	}

	pCharacter->m_PlayerFlags = GetPlayer()->m_PlayerFlags;
}

// DDRace

bool CCharacter::CanCollide(int ClientID)
{
	return Teams()->m_Core.CanCollide(GetPlayer()->GetCID(), ClientID);
}
bool CCharacter::SameTeam(int ClientID)
{
	return Teams()->m_Core.SameTeam(GetPlayer()->GetCID(), ClientID);
}

int CCharacter::Team()
{
	return Teams()->m_Core.Team(m_pPlayer->GetCID());
}

CGameTeams* CCharacter::Teams()
{
	return &((CGameControllerDDRace*)GameServer()->m_pController)->m_Teams;
}

void CCharacter::HandleBroadcast()
{
	if(Server()->Tick() - m_RefreshTime >= Server()->TickSpeed())
	{
		char aTmp[128];
		if( g_Config.m_SvBroadcast[0] != 0 && (Server()->Tick() > (m_LastBroadcast + (Server()->TickSpeed() * 9))))
		{
			str_format(aTmp, sizeof(aTmp), "%s", g_Config.m_SvBroadcast);
			GameServer()->SendBroadcast(aTmp, m_pPlayer->GetCID());
			m_LastBroadcast = Server()->Tick();
		}
		m_RefreshTime = Server()->Tick();
	}
}

void CCharacter::HandleSkippableTiles(int Index)
{

	// handle death-tiles and leaving gamelayer
	if((GameServer()->Collision()->GetCollisionAt(m_Pos.x+m_ProximityRadius/3.f, m_Pos.y-m_ProximityRadius/3.f)&CCollision::COLFLAG_DEATH ||
			GameServer()->Collision()->GetCollisionAt(m_Pos.x+m_ProximityRadius/3.f, m_Pos.y+m_ProximityRadius/3.f)&CCollision::COLFLAG_DEATH ||
			GameServer()->Collision()->GetCollisionAt(m_Pos.x-m_ProximityRadius/3.f, m_Pos.y-m_ProximityRadius/3.f)&CCollision::COLFLAG_DEATH ||
			GameServer()->Collision()->GetFCollisionAt(m_Pos.x+m_ProximityRadius/3.f, m_Pos.y-m_ProximityRadius/3.f)&CCollision::COLFLAG_DEATH ||
			GameServer()->Collision()->GetFCollisionAt(m_Pos.x+m_ProximityRadius/3.f, m_Pos.y+m_ProximityRadius/3.f)&CCollision::COLFLAG_DEATH ||
			GameServer()->Collision()->GetFCollisionAt(m_Pos.x-m_ProximityRadius/3.f, m_Pos.y-m_ProximityRadius/3.f)&CCollision::COLFLAG_DEATH ||
			GameServer()->Collision()->GetCollisionAt(m_Pos.x-m_ProximityRadius/3.f, m_Pos.y+m_ProximityRadius/3.f)&CCollision::COLFLAG_DEATH ||
			GameLayerClipped(m_Pos)))
			// !m_Super && !(Team() && Teams()->TeeFinished(m_pPlayer->GetCID())))
		{
			Die(m_pPlayer->GetCID(), WEAPON_WORLD);
			return;
		}

	if(Index < 0)
		return;

	// handle speedup tiles
	if(GameServer()->Collision()->IsSpeedup(Index))
	{
		vec2 Direction, MaxVel, TempVel = m_Core.m_Vel;
		int Force, MaxSpeed = 0;
		float TeeAngle, SpeederAngle, DiffAngle, SpeedLeft, TeeSpeed;
		GameServer()->Collision()->GetSpeedup(Index, &Direction, &Force, &MaxSpeed);
		if(Force == 255 && MaxSpeed)
		{
			m_Core.m_Vel = Direction * (MaxSpeed/5);
		}
		else
		{
			if(MaxSpeed > 0 && MaxSpeed < 5) MaxSpeed = 5;
			//dbg_msg("speedup tile start","Direction %f %f, Force %d, Max Speed %d", (Direction).x,(Direction).y, Force, MaxSpeed);
			if(MaxSpeed > 0)
			{
				if(Direction.x > 0.0000001f)
					SpeederAngle = -atan(Direction.y / Direction.x);
				else if(Direction.x < 0.0000001f)
					SpeederAngle = atan(Direction.y / Direction.x) + 2.0f * asin(1.0f);
				else if(Direction.y > 0.0000001f)
					SpeederAngle = asin(1.0f);
				else
					SpeederAngle = asin(-1.0f);

				if(SpeederAngle < 0)
					SpeederAngle = 4.0f * asin(1.0f) + SpeederAngle;

				if(TempVel.x > 0.0000001f)
					TeeAngle = -atan(TempVel.y / TempVel.x);
				else if(TempVel.x < 0.0000001f)
					TeeAngle = atan(TempVel.y / TempVel.x) + 2.0f * asin(1.0f);
				else if(TempVel.y > 0.0000001f)
					TeeAngle = asin(1.0f);
				else
					TeeAngle = asin(-1.0f);

				if(TeeAngle < 0)
					TeeAngle = 4.0f * asin(1.0f) + TeeAngle;

				TeeSpeed = sqrt(pow(TempVel.x, 2) + pow(TempVel.y, 2));

				DiffAngle = SpeederAngle - TeeAngle;
				SpeedLeft = MaxSpeed / 5.0f - cos(DiffAngle) * TeeSpeed;
				//dbg_msg("speedup tile debug","MaxSpeed %i, TeeSpeed %f, SpeedLeft %f, SpeederAngle %f, TeeAngle %f", MaxSpeed, TeeSpeed, SpeedLeft, SpeederAngle, TeeAngle);
				if(abs(SpeedLeft) > Force && SpeedLeft > 0.0000001f)
					TempVel += Direction * Force;
				else if(abs(SpeedLeft) > Force)
					TempVel += Direction * -Force;
				else
					TempVel += Direction * SpeedLeft;
			}
			else
				TempVel += Direction * Force;

			if(TempVel.x > 0 && ((m_TileIndex == TILE_STOP && m_TileFlags == ROTATION_270) || (m_TileIndexL == TILE_STOP && m_TileFlagsL == ROTATION_270) || (m_TileIndexL == TILE_STOPS && (m_TileFlagsL == ROTATION_90 || m_TileFlagsL ==ROTATION_270)) || (m_TileIndexL == TILE_STOPA) || (m_TileFIndex == TILE_STOP && m_TileFFlags == ROTATION_270) || (m_TileFIndexL == TILE_STOP && m_TileFFlagsL == ROTATION_270) || (m_TileFIndexL == TILE_STOPS && (m_TileFFlagsL == ROTATION_90 || m_TileFFlagsL == ROTATION_270)) || (m_TileFIndexL == TILE_STOPA) || (m_TileSIndex == TILE_STOP && m_TileSFlags == ROTATION_270) || (m_TileSIndexL == TILE_STOP && m_TileSFlagsL == ROTATION_270) || (m_TileSIndexL == TILE_STOPS && (m_TileSFlagsL == ROTATION_90 || m_TileSFlagsL == ROTATION_270)) || (m_TileSIndexL == TILE_STOPA)))
				TempVel.x = 0;
			if(TempVel.x < 0 && ((m_TileIndex == TILE_STOP && m_TileFlags == ROTATION_90) || (m_TileIndexR == TILE_STOP && m_TileFlagsR == ROTATION_90) || (m_TileIndexR == TILE_STOPS && (m_TileFlagsR == ROTATION_90 || m_TileFlagsR == ROTATION_270)) || (m_TileIndexR == TILE_STOPA) || (m_TileFIndex == TILE_STOP && m_TileFFlags == ROTATION_90) || (m_TileFIndexR == TILE_STOP && m_TileFFlagsR == ROTATION_90) || (m_TileFIndexR == TILE_STOPS && (m_TileFFlagsR == ROTATION_90 || m_TileFFlagsR == ROTATION_270)) || (m_TileFIndexR == TILE_STOPA) || (m_TileSIndex == TILE_STOP && m_TileSFlags == ROTATION_90) || (m_TileSIndexR == TILE_STOP && m_TileSFlagsR == ROTATION_90) || (m_TileSIndexR == TILE_STOPS && (m_TileSFlagsR == ROTATION_90 || m_TileSFlagsR == ROTATION_270)) || (m_TileSIndexR == TILE_STOPA)))
				TempVel.x = 0;
			if(TempVel.y < 0 && ((m_TileIndex == TILE_STOP && m_TileFlags == ROTATION_180) || (m_TileIndexB == TILE_STOP && m_TileFlagsB == ROTATION_180) || (m_TileIndexB == TILE_STOPS && (m_TileFlagsB == ROTATION_0 || m_TileFlagsB == ROTATION_180)) || (m_TileIndexB == TILE_STOPA) || (m_TileFIndex == TILE_STOP && m_TileFFlags == ROTATION_180) || (m_TileFIndexB == TILE_STOP && m_TileFFlagsB == ROTATION_180) || (m_TileFIndexB == TILE_STOPS && (m_TileFFlagsB == ROTATION_0 || m_TileFFlagsB == ROTATION_180)) || (m_TileFIndexB == TILE_STOPA) || (m_TileSIndex == TILE_STOP && m_TileSFlags == ROTATION_180) || (m_TileSIndexB == TILE_STOP && m_TileSFlagsB == ROTATION_180) || (m_TileSIndexB == TILE_STOPS && (m_TileSFlagsB == ROTATION_0 || m_TileSFlagsB == ROTATION_180)) || (m_TileSIndexB == TILE_STOPA)))
				TempVel.y = 0;
			if(TempVel.y > 0 && ((m_TileIndex == TILE_STOP && m_TileFlags == ROTATION_0) || (m_TileIndexT == TILE_STOP && m_TileFlagsT == ROTATION_0) || (m_TileIndexT == TILE_STOPS && (m_TileFlagsT == ROTATION_0 || m_TileFlagsT == ROTATION_180)) || (m_TileIndexT == TILE_STOPA) || (m_TileFIndex == TILE_STOP && m_TileFFlags == ROTATION_0) || (m_TileFIndexT == TILE_STOP && m_TileFFlagsT == ROTATION_0) || (m_TileFIndexT == TILE_STOPS && (m_TileFFlagsT == ROTATION_0 || m_TileFFlagsT == ROTATION_180)) || (m_TileFIndexT == TILE_STOPA) || (m_TileSIndex == TILE_STOP && m_TileSFlags == ROTATION_0) || (m_TileSIndexT == TILE_STOP && m_TileSFlagsT == ROTATION_0) || (m_TileSIndexT == TILE_STOPS && (m_TileSFlagsT == ROTATION_0 || m_TileSFlagsT == ROTATION_180)) || (m_TileSIndexT == TILE_STOPA)))
				TempVel.y = 0;
			m_Core.m_Vel = TempVel;
			//dbg_msg("speedup tile end","(Direction*Force) %f %f   m_Core.m_Vel%f %f",(Direction*Force).x,(Direction*Force).y,m_Core.m_Vel.x,m_Core.m_Vel.y);
			//dbg_msg("speedup tile end","Direction %f %f, Force %d, Max Speed %d", (Direction).x,(Direction).y, Force, MaxSpeed);
		}
	}
}
int cBody;
int cFeet;
void CCharacter::HandleTiles(int Index)
{
	CGameControllerDDRace* Controller = (CGameControllerDDRace*)GameServer()->m_pController;
	int MapIndex = Index;
	//int PureMapIndex = GameServer()->Collision()->GetPureMapIndex(m_Pos);
	float Offset = 4.0f;
	int MapIndexL = GameServer()->Collision()->GetPureMapIndex(vec2(m_Pos.x + (m_ProximityRadius / 2) + Offset, m_Pos.y));
	int MapIndexR = GameServer()->Collision()->GetPureMapIndex(vec2(m_Pos.x - (m_ProximityRadius / 2) - Offset, m_Pos.y));
	int MapIndexT = GameServer()->Collision()->GetPureMapIndex(vec2(m_Pos.x, m_Pos.y + (m_ProximityRadius / 2) + Offset));
	int MapIndexB = GameServer()->Collision()->GetPureMapIndex(vec2(m_Pos.x, m_Pos.y - (m_ProximityRadius / 2) - Offset));
	//dbg_msg("","N%d L%d R%d B%d T%d",MapIndex,MapIndexL,MapIndexR,MapIndexB,MapIndexT);
	m_TileIndex = GameServer()->Collision()->GetTileIndex(MapIndex);
	m_TileFlags = GameServer()->Collision()->GetTileFlags(MapIndex);
	m_TileIndexL = GameServer()->Collision()->GetTileIndex(MapIndexL);
	m_TileFlagsL = GameServer()->Collision()->GetTileFlags(MapIndexL);
	m_TileIndexR = GameServer()->Collision()->GetTileIndex(MapIndexR);
	m_TileFlagsR = GameServer()->Collision()->GetTileFlags(MapIndexR);
	m_TileIndexB = GameServer()->Collision()->GetTileIndex(MapIndexB);
	m_TileFlagsB = GameServer()->Collision()->GetTileFlags(MapIndexB);
	m_TileIndexT = GameServer()->Collision()->GetTileIndex(MapIndexT);
	m_TileFlagsT = GameServer()->Collision()->GetTileFlags(MapIndexT);
	m_TileFIndex = GameServer()->Collision()->GetFTileIndex(MapIndex);
	m_TileFFlags = GameServer()->Collision()->GetFTileFlags(MapIndex);
	m_TileFIndexL = GameServer()->Collision()->GetFTileIndex(MapIndexL);
	m_TileFFlagsL = GameServer()->Collision()->GetFTileFlags(MapIndexL);
	m_TileFIndexR = GameServer()->Collision()->GetFTileIndex(MapIndexR);
	m_TileFFlagsR = GameServer()->Collision()->GetFTileFlags(MapIndexR);
	m_TileFIndexB = GameServer()->Collision()->GetFTileIndex(MapIndexB);
	m_TileFFlagsB = GameServer()->Collision()->GetFTileFlags(MapIndexB);
	m_TileFIndexT = GameServer()->Collision()->GetFTileIndex(MapIndexT);
	m_TileFFlagsT = GameServer()->Collision()->GetFTileFlags(MapIndexT);//
	m_TileSIndex = (GameServer()->Collision()->m_pSwitchers && GameServer()->Collision()->m_pSwitchers[GameServer()->Collision()->GetDTileNumber(MapIndex)].m_Status[Team()])?(Team() != TEAM_SUPER)? GameServer()->Collision()->GetDTileIndex(MapIndex) : 0 : 0;
	m_TileSFlags = (GameServer()->Collision()->m_pSwitchers && GameServer()->Collision()->m_pSwitchers[GameServer()->Collision()->GetDTileNumber(MapIndex)].m_Status[Team()])?(Team() != TEAM_SUPER)? GameServer()->Collision()->GetDTileFlags(MapIndex) : 0 : 0;
	m_TileSIndexL = (GameServer()->Collision()->m_pSwitchers && GameServer()->Collision()->m_pSwitchers[GameServer()->Collision()->GetDTileNumber(MapIndexL)].m_Status[Team()])?(Team() != TEAM_SUPER)? GameServer()->Collision()->GetDTileIndex(MapIndexL) : 0 : 0;
	m_TileSFlagsL = (GameServer()->Collision()->m_pSwitchers && GameServer()->Collision()->m_pSwitchers[GameServer()->Collision()->GetDTileNumber(MapIndexL)].m_Status[Team()])?(Team() != TEAM_SUPER)? GameServer()->Collision()->GetDTileFlags(MapIndexL) : 0 : 0;
	m_TileSIndexR = (GameServer()->Collision()->m_pSwitchers && GameServer()->Collision()->m_pSwitchers[GameServer()->Collision()->GetDTileNumber(MapIndexR)].m_Status[Team()])?(Team() != TEAM_SUPER)? GameServer()->Collision()->GetDTileIndex(MapIndexR) : 0 : 0;
	m_TileSFlagsR = (GameServer()->Collision()->m_pSwitchers && GameServer()->Collision()->m_pSwitchers[GameServer()->Collision()->GetDTileNumber(MapIndexR)].m_Status[Team()])?(Team() != TEAM_SUPER)? GameServer()->Collision()->GetDTileFlags(MapIndexR) : 0 : 0;
	m_TileSIndexB = (GameServer()->Collision()->m_pSwitchers && GameServer()->Collision()->m_pSwitchers[GameServer()->Collision()->GetDTileNumber(MapIndexB)].m_Status[Team()])?(Team() != TEAM_SUPER)? GameServer()->Collision()->GetDTileIndex(MapIndexB) : 0 : 0;
	m_TileSFlagsB = (GameServer()->Collision()->m_pSwitchers && GameServer()->Collision()->m_pSwitchers[GameServer()->Collision()->GetDTileNumber(MapIndexB)].m_Status[Team()])?(Team() != TEAM_SUPER)? GameServer()->Collision()->GetDTileFlags(MapIndexB) : 0 : 0;
	m_TileSIndexT = (GameServer()->Collision()->m_pSwitchers && GameServer()->Collision()->m_pSwitchers[GameServer()->Collision()->GetDTileNumber(MapIndexT)].m_Status[Team()])?(Team() != TEAM_SUPER)? GameServer()->Collision()->GetDTileIndex(MapIndexT) : 0 : 0;
	m_TileSFlagsT = (GameServer()->Collision()->m_pSwitchers && GameServer()->Collision()->m_pSwitchers[GameServer()->Collision()->GetDTileNumber(MapIndexT)].m_Status[Team()])?(Team() != TEAM_SUPER)? GameServer()->Collision()->GetDTileFlags(MapIndexT) : 0 : 0;
	//dbg_msg("Tiles","%d, %d, %d, %d, %d", m_TileSIndex, m_TileSIndexL, m_TileSIndexR, m_TileSIndexB, m_TileSIndexT);
	//Sensitivity
	int S1 = GameServer()->Collision()->GetPureMapIndex(vec2(m_Pos.x + m_ProximityRadius / 3.f, m_Pos.y - m_ProximityRadius / 3.f));
	int S2 = GameServer()->Collision()->GetPureMapIndex(vec2(m_Pos.x + m_ProximityRadius / 3.f, m_Pos.y + m_ProximityRadius / 3.f));
	int S3 = GameServer()->Collision()->GetPureMapIndex(vec2(m_Pos.x - m_ProximityRadius / 3.f, m_Pos.y - m_ProximityRadius / 3.f));
	int S4 = GameServer()->Collision()->GetPureMapIndex(vec2(m_Pos.x - m_ProximityRadius / 3.f, m_Pos.y + m_ProximityRadius / 3.f));
	int Tile1 = GameServer()->Collision()->GetTileIndex(S1);
	int Tile2 = GameServer()->Collision()->GetTileIndex(S2);
	int Tile3 = GameServer()->Collision()->GetTileIndex(S3);
	int Tile4 = GameServer()->Collision()->GetTileIndex(S4);
	int FTile1 = GameServer()->Collision()->GetFTileIndex(S1);
	int FTile2 = GameServer()->Collision()->GetFTileIndex(S2);
	int FTile3 = GameServer()->Collision()->GetFTileIndex(S3);
	int FTile4 = GameServer()->Collision()->GetFTileIndex(S4);
	//dbg_msg("","N%d L%d R%d B%d T%d",m_TileIndex,m_TileIndexL,m_TileIndexR,m_TileIndexB,m_TileIndexT);
	//dbg_msg("","N%d L%d R%d B%d T%d",m_TileFIndex,m_TileFIndexL,m_TileFIndexR,m_TileFIndexB,m_TileFIndexT);
	if(Index < 0)
		return;
	int cp = GameServer()->Collision()->IsCheckpoint(MapIndex);
	if(cp != -1 && m_DDRaceState == DDRACE_STARTED && cp > m_CpActive)
	{
		m_CpActive = cp;
		m_CpCurrent[cp] = m_Time;
		m_CpTick = Server()->Tick() + Server()->TickSpeed() * 2;
		if(m_pPlayer->m_IsUsingDDRaceClient) {
			CPlayerData *pData = GameServer()->Score()->PlayerData(m_pPlayer->GetCID());
			CNetMsg_Sv_DDRaceTime Msg;
			Msg.m_Time = (int)m_Time;
			Msg.m_Check = 0;
			Msg.m_Finish = 0;

			if(m_CpActive != -1 && m_CpTick > Server()->Tick())
			{
				if(pData->m_BestTime && pData->m_aBestCpTime[m_CpActive] != 0)
				{
					float Diff = (m_CpCurrent[m_CpActive] - pData->m_aBestCpTime[m_CpActive])*100;
					Msg.m_Check = (int)Diff;
				}
			}

			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, m_pPlayer->GetCID());
		}
	}
	int cpf = GameServer()->Collision()->IsFCheckpoint(MapIndex);
	if(cpf != -1 && m_DDRaceState == DDRACE_STARTED && cpf > m_CpActive)
	{
		m_CpActive = cpf;
		m_CpCurrent[cpf] = m_Time;
		m_CpTick = Server()->Tick() + Server()->TickSpeed()*2;
		if(m_pPlayer->m_IsUsingDDRaceClient) {
			CPlayerData *pData = GameServer()->Score()->PlayerData(m_pPlayer->GetCID());
			CNetMsg_Sv_DDRaceTime Msg;
			Msg.m_Time = (int)m_Time;
			Msg.m_Check = 0;
			Msg.m_Finish = 0;

			if(m_CpActive != -1 && m_CpTick > Server()->Tick())
			{
				if(pData->m_BestTime && pData->m_aBestCpTime[m_CpActive] != 0)
				{
					float Diff = (m_CpCurrent[m_CpActive] - pData->m_aBestCpTime[m_CpActive])*100;
					Msg.m_Check = (int)Diff;
				}
			}

			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, m_pPlayer->GetCID());
		}
	}
	int tcp = GameServer()->Collision()->IsTCheckpoint(MapIndex);
	if(tcp)
		m_TeleCheckpoint = tcp;
	if(((m_TileIndex == TILE_BEGIN) || (m_TileFIndex == TILE_BEGIN) || FTile1 == TILE_BEGIN || FTile2 == TILE_BEGIN || FTile3 == TILE_BEGIN || FTile4 == TILE_BEGIN || Tile1 == TILE_BEGIN || Tile2 == TILE_BEGIN || Tile3 == TILE_BEGIN || Tile4 == TILE_BEGIN) && (m_DDRaceState == DDRACE_NONE || m_DDRaceState == DDRACE_FINISHED || (m_DDRaceState == DDRACE_STARTED && !Team())))
	{
        m_PosLas = vec2 (0,0);
		bool CanBegin = true;
		if(g_Config.m_SvResetPickus)
		{
			for (int i = WEAPON_SHOTGUN; i < NUM_WEAPONS; ++i)
			{
				m_aWeapons[i].m_Got = false;
				if(m_ActiveWeapon == i)
					m_ActiveWeapon = WEAPON_GUN;
			}
		}
		if(g_Config.m_SvTeam == 2 && (Team() == TEAM_FLOCK || Teams()->Count(Team()) <= 1))
		{
			if(m_LastStartWarning < Server()->Tick() - 3 * Server()->TickSpeed())
			{
				GameServer()->SendChatTarget(GetPlayer()->GetCID(),"Server admin requires you to be in a team and with other tees to start");
				m_LastStartWarning = Server()->Tick();
			}
			Die(GetPlayer()->GetCID(), WEAPON_WORLD);
			CanBegin = false;
		}
		if(CanBegin)
		{
			Teams()->OnCharacterStart(m_pPlayer->GetCID());
			m_CpActive = -2;
		} else {

		}

	}
	if(((m_TileIndex == TILE_END) || (m_TileFIndex == TILE_END) || FTile1 == TILE_END || FTile2 == TILE_END || FTile3 == TILE_END || FTile4 == TILE_END || Tile1 == TILE_END || Tile2 == TILE_END || Tile3 == TILE_END || Tile4 == TILE_END) && m_DDRaceState == DDRACE_STARTED)
		Controller->m_Teams.OnCharacterFinish(m_pPlayer->GetCID());
	if(((m_TileIndex == TILE_FREEZE) || (m_TileFIndex == TILE_FREEZE)) && !m_Super && !m_DeepFreeze)
		Freeze();
	else if(((m_TileIndex == TILE_UNFREEZE) || (m_TileFIndex == TILE_UNFREEZE)) && !m_DeepFreeze)
		UnFreeze();
	if(((m_TileIndex == TILE_DFREEZE) || (m_TileFIndex == TILE_DFREEZE)) && !m_Super && !m_DeepFreeze)
		m_DeepFreeze = true;
	else if(((m_TileIndex == TILE_DUNFREEZE) || (m_TileFIndex == TILE_DUNFREEZE)) && !m_Super && m_DeepFreeze)
		m_DeepFreeze = false;
	if(((m_TileIndex == TILE_EHOOK_START) || (m_TileFIndex == TILE_EHOOK_START)) && !m_EndlessHook)
	{
		GameServer()->SendChatTarget(GetPlayer()->GetCID(), "Endless hook has been activated");
		m_EndlessHook = true;
	}
	else if(((m_TileIndex == TILE_EHOOK_END) || (m_TileFIndex == TILE_EHOOK_END)) && m_EndlessHook)
	{
		GameServer()->SendChatTarget(GetPlayer()->GetCID(), "Endless hook has been deactivated");
		m_EndlessHook = false;
	}
	if(((m_TileIndex == TILE_HIT_START) || (m_TileFIndex == TILE_HIT_START)) && m_Hit != HIT_ALL)
	{
		GameServer()->SendChatTarget(GetPlayer()->GetCID(), "You can hit others");
		m_Hit = HIT_ALL;
	}
	else if(((m_TileIndex == TILE_HIT_END) || (m_TileFIndex == TILE_HIT_END)) && m_Hit != (DISABLE_HIT_GRENADE|DISABLE_HIT_HAMMER|DISABLE_HIT_RIFLE|DISABLE_HIT_SHOTGUN))
	{
		GameServer()->SendChatTarget(GetPlayer()->GetCID(), "You can't hit others");
		m_Hit = DISABLE_HIT_GRENADE|DISABLE_HIT_HAMMER|DISABLE_HIT_RIFLE|DISABLE_HIT_SHOTGUN;
	}
	if(((m_TileIndex == TILE_SOLO_START) || (m_TileFIndex == TILE_SOLO_START)) && !Teams()->m_Core.GetSolo(m_pPlayer->GetCID()))
	{
		GameServer()->SendChatTarget(GetPlayer()->GetCID(), "You are now in a solo part.");
		Teams()->m_Core.SetSolo(m_pPlayer->GetCID(), true);
	}
	else if(((m_TileIndex == TILE_SOLO_END) || (m_TileFIndex == TILE_SOLO_END)) && Teams()->m_Core.GetSolo(m_pPlayer->GetCID()))
	{
		GameServer()->SendChatTarget(GetPlayer()->GetCID(), "You are now out of the solo part.");
		Teams()->m_Core.SetSolo(m_pPlayer->GetCID(), false);
	}
	if(((m_TileIndex == TILE_STOP && m_TileFlags == ROTATION_270) || (m_TileIndexL == TILE_STOP && m_TileFlagsL == ROTATION_270) || (m_TileIndexL == TILE_STOPS && (m_TileFlagsL == ROTATION_90 || m_TileFlagsL ==ROTATION_270)) || (m_TileIndexL == TILE_STOPA) || (m_TileFIndex == TILE_STOP && m_TileFFlags == ROTATION_270) || (m_TileFIndexL == TILE_STOP && m_TileFFlagsL == ROTATION_270) || (m_TileFIndexL == TILE_STOPS && (m_TileFFlagsL == ROTATION_90 || m_TileFFlagsL == ROTATION_270)) || (m_TileFIndexL == TILE_STOPA) || (m_TileSIndex == TILE_STOP && m_TileSFlags == ROTATION_270) || (m_TileSIndexL == TILE_STOP && m_TileSFlagsL == ROTATION_270) || (m_TileSIndexL == TILE_STOPS && (m_TileSFlagsL == ROTATION_90 || m_TileSFlagsL == ROTATION_270)) || (m_TileSIndexL == TILE_STOPA)) && m_Core.m_Vel.x > 0)
	{
		if((int)GameServer()->Collision()->GetPos(MapIndexL).x)
			if((int)GameServer()->Collision()->GetPos(MapIndexL).x < (int)m_Core.m_Pos.x)
				m_Core.m_Pos = m_PrevPos;
		m_Core.m_Vel.x = 0;
	}
	if(((m_TileIndex == TILE_STOP && m_TileFlags == ROTATION_90) || (m_TileIndexR == TILE_STOP && m_TileFlagsR == ROTATION_90) || (m_TileIndexR == TILE_STOPS && (m_TileFlagsR == ROTATION_90 || m_TileFlagsR == ROTATION_270)) || (m_TileIndexR == TILE_STOPA) || (m_TileFIndex == TILE_STOP && m_TileFFlags == ROTATION_90) || (m_TileFIndexR == TILE_STOP && m_TileFFlagsR == ROTATION_90) || (m_TileFIndexR == TILE_STOPS && (m_TileFFlagsR == ROTATION_90 || m_TileFFlagsR == ROTATION_270)) || (m_TileFIndexR == TILE_STOPA) || (m_TileSIndex == TILE_STOP && m_TileSFlags == ROTATION_90) || (m_TileSIndexR == TILE_STOP && m_TileSFlagsR == ROTATION_90) || (m_TileSIndexR == TILE_STOPS && (m_TileSFlagsR == ROTATION_90 || m_TileSFlagsR == ROTATION_270)) || (m_TileSIndexR == TILE_STOPA)) && m_Core.m_Vel.x < 0)
	{
		if((int)GameServer()->Collision()->GetPos(MapIndexR).x)
			if((int)GameServer()->Collision()->GetPos(MapIndexR).x > (int)m_Core.m_Pos.x)
				m_Core.m_Pos = m_PrevPos;
		m_Core.m_Vel.x = 0;
	}
	if(((m_TileIndex == TILE_STOP && m_TileFlags == ROTATION_180) || (m_TileIndexB == TILE_STOP && m_TileFlagsB == ROTATION_180) || (m_TileIndexB == TILE_STOPS && (m_TileFlagsB == ROTATION_0 || m_TileFlagsB == ROTATION_180)) || (m_TileIndexB == TILE_STOPA) || (m_TileFIndex == TILE_STOP && m_TileFFlags == ROTATION_180) || (m_TileFIndexB == TILE_STOP && m_TileFFlagsB == ROTATION_180) || (m_TileFIndexB == TILE_STOPS && (m_TileFFlagsB == ROTATION_0 || m_TileFFlagsB == ROTATION_180)) || (m_TileFIndexB == TILE_STOPA) || (m_TileSIndex == TILE_STOP && m_TileSFlags == ROTATION_180) || (m_TileSIndexB == TILE_STOP && m_TileSFlagsB == ROTATION_180) || (m_TileSIndexB == TILE_STOPS && (m_TileSFlagsB == ROTATION_0 || m_TileSFlagsB == ROTATION_180)) || (m_TileSIndexB == TILE_STOPA)) && m_Core.m_Vel.y < 0)
	{
		if((int)GameServer()->Collision()->GetPos(MapIndexB).y)
			if((int)GameServer()->Collision()->GetPos(MapIndexB).y > (int)m_Core.m_Pos.y)
				m_Core.m_Pos = m_PrevPos;
		m_Core.m_Vel.y = 0;
	}
	if(((m_TileIndex == TILE_STOP && m_TileFlags == ROTATION_0) || (m_TileIndexT == TILE_STOP && m_TileFlagsT == ROTATION_0) || (m_TileIndexT == TILE_STOPS && (m_TileFlagsT == ROTATION_0 || m_TileFlagsT == ROTATION_180)) || (m_TileIndexT == TILE_STOPA) || (m_TileFIndex == TILE_STOP && m_TileFFlags == ROTATION_0) || (m_TileFIndexT == TILE_STOP && m_TileFFlagsT == ROTATION_0) || (m_TileFIndexT == TILE_STOPS && (m_TileFFlagsT == ROTATION_0 || m_TileFFlagsT == ROTATION_180)) || (m_TileFIndexT == TILE_STOPA) || (m_TileSIndex == TILE_STOP && m_TileSFlags == ROTATION_0) || (m_TileSIndexT == TILE_STOP && m_TileSFlagsT == ROTATION_0) || (m_TileSIndexT == TILE_STOPS && (m_TileSFlagsT == ROTATION_0 || m_TileSFlagsT == ROTATION_180)) || (m_TileSIndexT == TILE_STOPA)) && m_Core.m_Vel.y > 0)
	{
		//dbg_msg("","%f %f",GameServer()->Collision()->GetPos(MapIndex).y,m_Core.m_Pos.y);
		if((int)GameServer()->Collision()->GetPos(MapIndexT).y)
			if((int)GameServer()->Collision()->GetPos(MapIndexT).y < (int)m_Core.m_Pos.y)
				m_Core.m_Pos = m_PrevPos;
		m_Core.m_Vel.y = 0;
		m_Core.m_Jumped = 0;
	}









	//XXLmod
	if(((m_TileIndex == TILE_RAINBOW) || (m_TileFIndex == TILE_RAINBOW)))
	{
		if (m_LastIndexTile == TILE_RAINBOW || m_LastIndexFrontTile == TILE_RAINBOW)
			return;
		char aBuf[256];
		if (m_pPlayer->m_Rainbow)
		{
            m_pPlayer->m_TeeInfos.m_ColorBody = cBody;
            m_pPlayer->m_TeeInfos.m_ColorFeet = cFeet;
			m_pPlayer->m_Rainbow = false;
			str_format(aBuf, sizeof(aBuf), "Rainbow disabled");
		}
		else
		{
            cBody = m_pPlayer->m_TeeInfos.m_ColorBody;
            cFeet = m_pPlayer->m_TeeInfos.m_ColorFeet;
			m_pPlayer->m_Rainbow = true;
			str_format(aBuf, sizeof(aBuf), "Rainbow enabled");
		}

			GameServer()->SendChatTarget(m_pPlayer->GetCID(), aBuf);
	}
	if(((m_TileIndex == TILE_XXL) || (m_TileFIndex == TILE_XXL)))
	{
		if (m_LastIndexTile == TILE_XXL || m_LastIndexFrontTile == TILE_XXL)
			return;

		char aBuf[256];
		if (m_FastReload)
		{
			m_FastReload = false;
			m_ReloadMultiplier = 1000;
			str_format(aBuf, sizeof(aBuf), "XXL disabled");
		}
		else
		{
			m_FastReload = true;
			m_ReloadMultiplier = g_Config.m_SvXXLSpeed;
			str_format(aBuf, sizeof(aBuf), "XXL enabled");
		}

		GameServer()->SendChatTarget(m_pPlayer->GetCID(), aBuf);
	}

	if(((m_TileIndex == TILE_ADMIN) || (m_TileFIndex == TILE_ADMIN)))
	{
		if (GetPlayer()->m_Authed < 4)
		{
            char aBuf[256];
			Die(m_pPlayer->GetCID(), WEAPON_WORLD);
			// str_format(aBuf, sizeof(aBuf), "Admins only! Your rank: %i", GetPlayer()->m_Authed);
            str_format(aBuf, sizeof(aBuf), "Admins room, you can't enter");
            GameServer()->SendChatTarget(m_pPlayer->GetCID(), aBuf);
		}
	}

	if(((m_TileIndex == TILE_MEMBER) || (m_TileFIndex == TILE_MEMBER)))
	{
        if (GetPlayer()->m_Authed < 3)
        {
            Die(m_pPlayer->GetCID(), WEAPON_WORLD);
            char aBuf[256];
            // str_format(aBuf, sizeof(aBuf), "Members only! Your rank: %i", GetPlayer()->m_Authed);
            str_format(aBuf, sizeof(aBuf), "Members room, you can't enter");
            GameServer()->SendChatTarget(m_pPlayer->GetCID(), aBuf);
        }
	}
    if(((m_TileIndex == TILE_HELPER) || (m_TileFIndex == TILE_HELPER)))
    {
        if (GetPlayer()->m_Authed  < 2 || GetPlayer()->m_Authed == 3)
        {
            Die(m_pPlayer->GetCID(), WEAPON_WORLD);
            char aBuf[128];
            // str_format(aBuf, sizeof(aBuf), "Helpers only! Your rank: %i", GetPlayer()->m_Authed);
            str_format(aBuf, sizeof(aBuf), "Helper room, you can't enter");
            GameServer()->SendChatTarget(m_pPlayer->GetCID(), aBuf);
        }
    }
    if(((m_TileIndex == TILE_KID) || (m_TileFIndex == TILE_KID)))
    {
        if (GetPlayer()->m_Authed  < 1)
        {
            Die(m_pPlayer->GetCID(), WEAPON_WORLD);
            char aBuf[64];
            str_format(aBuf, sizeof(aBuf), "Kid's room, you can't enter");
            GameServer()->SendChatTarget(m_pPlayer->GetCID(), aBuf);
        }
    }
	if(((m_TileIndex == TILE_SUPER) || (m_TileFIndex == TILE_SUPER)))
	{
		if (m_LastIndexTile == TILE_SUPER || m_LastIndexFrontTile == TILE_SUPER)
			return;

		char aBuf[64];
		if (m_Super){
			m_Super = false;
			str_format(aBuf, sizeof(aBuf), "Super disabled");
		}
		else
		{
			m_Super = true;
			UnFreeze();
			str_format(aBuf, sizeof(aBuf), "Super enabled");
		}
		GameServer()->SendChatTarget(m_pPlayer->GetCID(), aBuf);
	}

	if(((m_TileIndex == TILE_HAMMER) || (m_TileFIndex == TILE_HAMMER)))
	{
		if (m_LastIndexTile == TILE_HAMMER || m_LastIndexFrontTile == TILE_HAMMER)
			return;

		char aBuf[64];
		if (m_HammerType == 3)
		{
			m_HammerType = 0;
			str_format(aBuf, sizeof(aBuf), "HeavyHammer disabled");
		}else{
			m_HammerType = 3;
			str_format(aBuf, sizeof(aBuf), "HeavyHammer enabled");
		}

		GameServer()->SendChatTarget(m_pPlayer->GetCID(), aBuf);
	}

	if(((m_TileIndex == TILE_BLOODY) || (m_TileFIndex == TILE_BLOODY)))
	{
		if (m_LastIndexTile == TILE_BLOODY || m_LastIndexFrontTile == TILE_BLOODY)
			return;

		char aBuf[64];
		if (m_Bloody)
		{
			m_Bloody = false;
			str_format(aBuf, sizeof(aBuf), "Bloody disabled");
		}
		else
		{
			m_Bloody = true;
			str_format(aBuf, sizeof(aBuf), "Bloody enabled");
		}

			GameServer()->SendChatTarget(m_pPlayer->GetCID(), aBuf);
	}


	if(((m_TileIndex == TILE_RMEXTRAS) || (m_TileFIndex == TILE_RMEXTRAS)))
	{
		if (m_LastIndexTile == TILE_RMEXTRAS || m_LastIndexFrontTile == TILE_RMEXTRAS)
			return;

        if( m_pPlayer->m_Rainbow ||
            m_FastReload ||
            m_Super ||
            m_HammerType != 0 ||
            m_Bloody ||
            m_pPlayer->m_Invisible ||
            m_EHammer ||
            m_gHammer ||
            m_THammer ||
            m_cHammer ||
            m_hexp ||
            m_SpreadGun ||
            m_SpreadShotgun ||
            m_SpreadGrenade ||
            m_SpreadLaser ||
            m_gBounce ||
            m_pLaser ||
            m_gunexp ||
            m_pPlayer->m_Rainbow
        ) {
            m_EHammer = false;
            m_gHammer = false;
            m_THammer = false;
            m_cHammer = false;
            m_hexp = 0;
            m_gunexp = 0;
            m_SpreadGun = false;
            m_SpreadShotgun = false;
            m_SpreadGrenade = false;
            m_SpreadLaser = false;
            m_gBounce = false;
            m_pLaser = false;
            m_pPlayer->m_Rainbow = false;
            m_FastReload = false;
            m_ReloadMultiplier = 1000;
            m_Super = false;
            m_HammerType = 0;
            m_Bloody = false;
            m_pPlayer->m_Invisible = false;
            char aBuf[64];
            str_format(aBuf, sizeof(aBuf), "ALL extras disabled");
            GameServer()->SendChatTarget(m_pPlayer->GetCID(), aBuf);
        }
	}

	if(((m_TileIndex == TILE_RMNINJA) || (m_TileFIndex == TILE_RMNINJA)))
	{
		if (m_LastIndexTile == TILE_RMNINJA || m_LastIndexFrontTile == TILE_RMNINJA || m_ActiveWeapon != WEAPON_NINJA)
			return;

		m_aWeapons[WEAPON_NINJA].m_Got = false;
		m_ActiveWeapon = m_LastWeapon;
		if(m_ActiveWeapon == WEAPON_NINJA)
			m_ActiveWeapon = WEAPON_GUN;

		SetWeapon(m_ActiveWeapon);

		if (g_Config.m_SvRMNinjaResetVel)
			m_Core.m_Vel = vec2 (0,0);

		GameServer()->SendChatTarget(m_pPlayer->GetCID(), "You lost ninja!");
	}

	if(((m_TileIndex == TILE_INVIS) || (m_TileFIndex == TILE_INVIS)))
	{
		if (m_LastIndexTile == TILE_INVIS || m_LastIndexFrontTile == TILE_INVIS)
			return;

		char aBuf[64];
		if (m_pPlayer->m_Invisible)
		{
			m_pPlayer->m_Invisible = false;
			str_format(aBuf, sizeof(aBuf), "Invisible disabled");
		}
		else{
			m_pPlayer->m_Invisible = true;
			str_format(aBuf, sizeof(aBuf), "Invisible enabled");
		}

			GameServer()->SendChatTarget(m_pPlayer->GetCID(), aBuf);
	}

	if(((m_TileIndex == TILE_ENTER) || (m_TileFIndex == TILE_ENTER)))
	{
        if (m_LastIndexTile == TILE_ENTER || m_LastIndexFrontTile == TILE_ENTER)
            return;

		if(m_CheckMove == 0) {
			m_CheckMove = 1;
			//GameServer()->SendChatTarget(GetPlayer()->GetCID(), "You can enter.");
		}
		else {
            // return;
		}
	}

	if(((m_TileIndex == TILE_EXIT) || (m_TileFIndex == TILE_EXIT)))
	{
        if (m_LastIndexTile == TILE_EXIT || m_LastIndexFrontTile == TILE_EXIT)
            return;

		if(m_CheckMove) {
			m_CheckMove = 0;
			//GameServer()->SendChatTarget(GetPlayer()->GetCID(), "You have left.");
		}
	}
	if(((m_TileIndex == TILE_MOVE) || (m_TileFIndex == TILE_MOVE)))
	{
        if (m_LastIndexTile == TILE_MOVE || m_LastIndexFrontTile == TILE_MOVE)
            return;

		if(!m_CheckMove){
            Die(GetPlayer()->GetCID(), WEAPON_WORLD);
            //GameServer()->SendChatTarget(GetPlayer()->GetCID(), "You can not enter.");
        }
	}
    /**/



    if(((m_TileIndex == TILE_HEXP) || (m_TileFIndex == TILE_HEXP)))
    {
        if (m_LastIndexTile == TILE_HEXP || m_LastIndexFrontTile == TILE_HEXP)
            return;

        if(m_hexp == 1) {
            m_hexp = 0;
            char aBuf[64];
            str_format(aBuf, sizeof(aBuf), "Hammer explosive disabled");
            GameServer()->SendChatTarget(m_pPlayer->GetCID(), aBuf);
        }
        else {
            m_hexp = 1;
            char aBuf[64];
            str_format(aBuf, sizeof(aBuf), "Hammer explosive enabled");
            GameServer()->SendChatTarget(m_pPlayer->GetCID(), aBuf);
        }

    }
    if(((m_TileIndex == TILE_GBOUNCE) || (m_TileFIndex == TILE_GBOUNCE)))
    {
        if (m_LastIndexTile == TILE_GBOUNCE || m_LastIndexFrontTile == TILE_GBOUNCE)
            return;

        if(m_gBounce) {
            m_gBounce = false;
            char aBuf[64];
            str_format(aBuf, sizeof(aBuf), "Grenade bounce disabled");
            GameServer()->SendChatTarget(m_pPlayer->GetCID(), aBuf);
        }
        else {
            m_gBounce = true;
            char aBuf[64];
            str_format(aBuf, sizeof(aBuf), "Grenade bounce enabled");
            GameServer()->SendChatTarget(m_pPlayer->GetCID(), aBuf);
        }
    }
    if(((m_TileIndex == TILE_GUNSPREAD) || (m_TileFIndex == TILE_GUNSPREAD)))
    {
        if (m_LastIndexTile == TILE_GUNSPREAD || m_LastIndexFrontTile == TILE_GUNSPREAD)
            return;

        if(m_SpreadGun != 0) {
            m_SpreadGun = 0;
            char aBuf[64];
            str_format(aBuf, sizeof(aBuf), "Gun Spread disabled");
            GameServer()->SendChatTarget(m_pPlayer->GetCID(), aBuf);
        }
        else {
            m_SpreadGun = 5;
            char aBuf[64];
            str_format(aBuf, sizeof(aBuf), "Gun Spread enabled");
            GameServer()->SendChatTarget(m_pPlayer->GetCID(), aBuf);
        }
    }
    if(((m_TileIndex == TILE_SHOTGUNSPREAD) || (m_TileFIndex == TILE_SHOTGUNSPREAD)))
    {
        if (m_LastIndexTile == TILE_SHOTGUNSPREAD || m_LastIndexFrontTile == TILE_SHOTGUNSPREAD)
            return;

        if(m_SpreadShotgun != 0) {
            m_SpreadShotgun = 0;
            char aBuf[64];
            str_format(aBuf, sizeof(aBuf), "Shotgun spread disabled");
            GameServer()->SendChatTarget(m_pPlayer->GetCID(), aBuf);
        }
        else {
            m_SpreadShotgun = 5;
            char aBuf[64];
            str_format(aBuf, sizeof(aBuf), "Shotgun spread enabled");
            GameServer()->SendChatTarget(m_pPlayer->GetCID(), aBuf);
        }
    }
    if(((m_TileIndex == TILE_GRENADESPREAD) || (m_TileFIndex == TILE_GRENADESPREAD)))
    {
        if (m_LastIndexTile == TILE_GRENADESPREAD || m_LastIndexFrontTile == TILE_GRENADESPREAD)
            return;

        if(m_SpreadGrenade != 0) {
            m_SpreadGrenade = 0;
            char aBuf[64];
            str_format(aBuf, sizeof(aBuf), "Grenade spread disabled");
            GameServer()->SendChatTarget(m_pPlayer->GetCID(), aBuf);
        }
        else {
            m_SpreadGrenade = 5;
            char aBuf[64];
            str_format(aBuf, sizeof(aBuf), "Grenade spread enabled");
            GameServer()->SendChatTarget(m_pPlayer->GetCID(), aBuf);
        }
    }
    if(((m_TileIndex == TILE_LASERSPREAD) || (m_TileFIndex == TILE_LASERSPREAD)))
    {
        if (m_LastIndexTile == TILE_LASERSPREAD || m_LastIndexFrontTile == TILE_LASERSPREAD)
            return;

        if(m_SpreadLaser != 0) {
            m_SpreadLaser = 0;
            char aBuf[64];
            str_format(aBuf, sizeof(aBuf), "Laser spread disabled");
            GameServer()->SendChatTarget(m_pPlayer->GetCID(), aBuf);
        }
        else {
            m_SpreadLaser = 5;
            char aBuf[64];
            str_format(aBuf, sizeof(aBuf), "Laser spread enabled");
            GameServer()->SendChatTarget(m_pPlayer->GetCID(), aBuf);
        }
    }




    /**/
    //jDDRace
    if(((m_TileIndex == TILE_JUMPS_DEFAULT) || (m_TileFIndex == TILE_JUMPS_DEFAULT))) //87
    {
        if (m_LastIndexTile == TILE_JUMPS_DEFAULT || m_LastIndexFrontTile == TILE_JUMPS_DEFAULT)
            return;

        m_Core.m_MaxJumps = 2; //default
            char aBuf[256];
                str_format(aBuf, sizeof(aBuf), "You have %d jump(s)",m_Core.m_MaxJumps);
                GameServer()->SendChatTarget(m_pPlayer->GetCID(), aBuf);
    }
    if(((m_TileIndex == TILE_JUMPS_ADD) || (m_TileFIndex == TILE_JUMPS_ADD))) //87
    {
        if (m_LastIndexTile == TILE_JUMPS_ADD || m_LastIndexFrontTile == TILE_JUMPS_ADD)
            return;

        m_Core.m_MaxJumps++; //add a jump
            char aBuf[256];
                str_format(aBuf, sizeof(aBuf), "You have %d jump(s)",m_Core.m_MaxJumps);
                GameServer()->SendChatTarget(m_pPlayer->GetCID(), aBuf);
    }
    if(((m_TileIndex == TILE_JUMPS_REMOVE) || (m_TileFIndex == TILE_JUMPS_REMOVE))) //87
    {
        if (m_LastIndexTile == TILE_JUMPS_REMOVE || m_LastIndexFrontTile == TILE_JUMPS_REMOVE)
            return;
        if (m_Core.m_MaxJumps > 1)
            m_Core.m_MaxJumps--; //remove a jump
            char aBuf[256];
                str_format(aBuf, sizeof(aBuf), "You have %d jump(s)",m_Core.m_MaxJumps);
                GameServer()->SendChatTarget(m_pPlayer->GetCID(), aBuf);
    }



	//First time here?
	m_LastIndexTile = m_TileIndex;
	m_LastIndexFrontTile = m_TileFIndex;

	//-----




	// handle switch tiles
	if(GameServer()->Collision()->IsSwitch(MapIndex) == TILE_SWITCHOPEN && Team() != TEAM_SUPER)
	{
		GameServer()->Collision()->m_pSwitchers[GameServer()->Collision()->GetSwitchNumber(MapIndex)].m_Status[Team()] = true;
		GameServer()->Collision()->m_pSwitchers[GameServer()->Collision()->GetSwitchNumber(MapIndex)].m_EndTick[Team()] = 0;
		GameServer()->Collision()->m_pSwitchers[GameServer()->Collision()->GetSwitchNumber(MapIndex)].m_Type[Team()] = TILE_SWITCHOPEN;
	}
	else if(GameServer()->Collision()->IsSwitch(MapIndex) == TILE_SWITCHTIMEDOPEN && Team() != TEAM_SUPER)
	{
		GameServer()->Collision()->m_pSwitchers[GameServer()->Collision()->GetSwitchNumber(MapIndex)].m_Status[Team()] = true;
		GameServer()->Collision()->m_pSwitchers[GameServer()->Collision()->GetSwitchNumber(MapIndex)].m_EndTick[Team()] = Server()->Tick() + 1 + GameServer()->Collision()->GetSwitchDelay(MapIndex)*Server()->TickSpeed() ;
		GameServer()->Collision()->m_pSwitchers[GameServer()->Collision()->GetSwitchNumber(MapIndex)].m_Type[Team()] = TILE_SWITCHTIMEDOPEN;
	}
	else if(GameServer()->Collision()->IsSwitch(MapIndex) == TILE_SWITCHTIMEDCLOSE && Team() != TEAM_SUPER)
	{
		GameServer()->Collision()->m_pSwitchers[GameServer()->Collision()->GetSwitchNumber(MapIndex)].m_Status[Team()] = false;
		GameServer()->Collision()->m_pSwitchers[GameServer()->Collision()->GetSwitchNumber(MapIndex)].m_EndTick[Team()] = Server()->Tick() + 1 + GameServer()->Collision()->GetSwitchDelay(MapIndex)*Server()->TickSpeed();
		GameServer()->Collision()->m_pSwitchers[GameServer()->Collision()->GetSwitchNumber(MapIndex)].m_Type[Team()] = TILE_SWITCHTIMEDCLOSE;
	}
	else if(GameServer()->Collision()->IsSwitch(MapIndex) == TILE_SWITCHCLOSE && Team() != TEAM_SUPER)
	{
		GameServer()->Collision()->m_pSwitchers[GameServer()->Collision()->GetSwitchNumber(MapIndex)].m_Status[Team()] = false;
		GameServer()->Collision()->m_pSwitchers[GameServer()->Collision()->GetSwitchNumber(MapIndex)].m_EndTick[Team()] = 0;
		GameServer()->Collision()->m_pSwitchers[GameServer()->Collision()->GetSwitchNumber(MapIndex)].m_Type[Team()] = TILE_SWITCHCLOSE;
	}
	else if(GameServer()->Collision()->IsSwitch(MapIndex) == TILE_FREEZE && Team() != TEAM_SUPER)
	{
		Freeze(GameServer()->Collision()->GetSwitchDelay(MapIndex));
	}
	else if(GameServer()->Collision()->IsSwitch(MapIndex) == TILE_DFREEZE && Team() != TEAM_SUPER && GameServer()->Collision()->m_pSwitchers[GameServer()->Collision()->GetSwitchNumber(MapIndex)].m_Status[Team()])
	{
		m_DeepFreeze = true;
	}
	else if(GameServer()->Collision()->IsSwitch(MapIndex) == TILE_DUNFREEZE && Team() != TEAM_SUPER && GameServer()->Collision()->m_pSwitchers[GameServer()->Collision()->GetSwitchNumber(MapIndex)].m_Status[Team()])
	{
		m_DeepFreeze = false;
	}
	else if(GameServer()->Collision()->IsSwitch(MapIndex) == TILE_HIT_START && m_Hit&DISABLE_HIT_HAMMER && GameServer()->Collision()->GetSwitchDelay(MapIndex) == WEAPON_HAMMER)
	{
		GameServer()->SendChatTarget(GetPlayer()->GetCID(),"You can hammer hit others");
		m_Hit ^= DISABLE_HIT_HAMMER;
	}
	else if(GameServer()->Collision()->IsSwitch(MapIndex) == TILE_HIT_END && !(m_Hit&DISABLE_HIT_HAMMER) && GameServer()->Collision()->GetSwitchDelay(MapIndex) == WEAPON_HAMMER)
	{
		GameServer()->SendChatTarget(GetPlayer()->GetCID(),"You can't hammer hit others");
		m_Hit |= DISABLE_HIT_HAMMER;
	}
	else if(GameServer()->Collision()->IsSwitch(MapIndex) == TILE_HIT_START && m_Hit&DISABLE_HIT_SHOTGUN && GameServer()->Collision()->GetSwitchDelay(MapIndex) == WEAPON_SHOTGUN)
	{
		GameServer()->SendChatTarget(GetPlayer()->GetCID(),"You can shoot others with shotgun");
		m_Hit ^= DISABLE_HIT_SHOTGUN;
	}
	else if(GameServer()->Collision()->IsSwitch(MapIndex) == TILE_HIT_END && !(m_Hit&DISABLE_HIT_SHOTGUN) && GameServer()->Collision()->GetSwitchDelay(MapIndex) == WEAPON_SHOTGUN)
	{
		GameServer()->SendChatTarget(GetPlayer()->GetCID(),"You can't shoot others with shotgun");
		m_Hit |= DISABLE_HIT_SHOTGUN;
	}
	else if(GameServer()->Collision()->IsSwitch(MapIndex) == TILE_HIT_START && m_Hit&DISABLE_HIT_GRENADE && GameServer()->Collision()->GetSwitchDelay(MapIndex) == WEAPON_GRENADE)
	{
		GameServer()->SendChatTarget(GetPlayer()->GetCID(),"You can shoot others with grenade");
		m_Hit ^= DISABLE_HIT_GRENADE;
	}
	else if(GameServer()->Collision()->IsSwitch(MapIndex) == TILE_HIT_END && !(m_Hit&DISABLE_HIT_GRENADE) && GameServer()->Collision()->GetSwitchDelay(MapIndex) == WEAPON_GRENADE)
	{
		GameServer()->SendChatTarget(GetPlayer()->GetCID(),"You can't shoot others with grenade");
		m_Hit |= DISABLE_HIT_GRENADE;
	}
	else if(GameServer()->Collision()->IsSwitch(MapIndex) == TILE_HIT_START && m_Hit&DISABLE_HIT_RIFLE && GameServer()->Collision()->GetSwitchDelay(MapIndex) == WEAPON_RIFLE)
	{
		GameServer()->SendChatTarget(GetPlayer()->GetCID(),"You can shoot others with rifle");
		m_Hit ^= DISABLE_HIT_RIFLE;
	}
	else if(GameServer()->Collision()->IsSwitch(MapIndex) == TILE_HIT_END && !(m_Hit&DISABLE_HIT_RIFLE) && GameServer()->Collision()->GetSwitchDelay(MapIndex) == WEAPON_RIFLE)
	{
		GameServer()->SendChatTarget(GetPlayer()->GetCID(),"You can't shoot others with rifle");
		m_Hit |= DISABLE_HIT_RIFLE;
	}
	int z = GameServer()->Collision()->IsTeleport(MapIndex);
	if(z && Controller->m_TeleOuts[z-1].size())
	{
		m_Core.m_HookedPlayer = -1;
		m_Core.m_HookState = HOOK_RETRACTED;
		m_Core.m_TriggeredEvents |= COREEVENT_HOOK_RETRACT;
		m_Core.m_HookState = HOOK_RETRACTED;
		int Num = Controller->m_TeleOuts[z-1].size();
		m_Core.m_Pos = Controller->m_TeleOuts[z-1][(!Num)?Num:rand() % Num];
		m_Core.m_HookPos = m_Core.m_Pos;
		return;
	}
	int evilz = GameServer()->Collision()->IsEvilTeleport(MapIndex);
	if(evilz && !m_Super && Controller->m_TeleOuts[evilz-1].size())
	{
		m_Core.m_HookedPlayer = -1;
		m_Core.m_HookState = HOOK_RETRACTED;
		m_Core.m_TriggeredEvents |= COREEVENT_HOOK_RETRACT;
		m_Core.m_HookState = HOOK_RETRACTED;
		GameWorld()->ReleaseHooked(GetPlayer()->GetCID());
		int Num = Controller->m_TeleOuts[evilz-1].size();
		m_Core.m_Pos = Controller->m_TeleOuts[evilz-1][(!Num)?Num:rand() % Num];
		m_Core.m_HookPos = m_Core.m_Pos;
		m_Core.m_Vel = vec2(0,0);
		return;
	}
	if(GameServer()->Collision()->IsCheckTeleport(MapIndex))
	{
		// first check if there is a TeleCheckOut for the current recorded checkpoint, if not check previous checkpoints
		for(int k=m_TeleCheckpoint-1; k >= 0; k--)
		{
			if(Controller->m_TeleCheckOuts[k].size())
			{
				m_Core.m_HookedPlayer = -1;
				m_Core.m_HookState = HOOK_RETRACTED;
				m_Core.m_TriggeredEvents |= COREEVENT_HOOK_RETRACT;
				m_Core.m_HookState = HOOK_RETRACTED;
				int Num = Controller->m_TeleCheckOuts[k].size();
				m_Core.m_Pos = Controller->m_TeleCheckOuts[k][(!Num)?Num:rand() % Num];
				m_Core.m_HookPos = m_Core.m_Pos;
				return;
			}
		}
		// if no checkpointout have been found (or if there no recorded checkpoint), teleport to start
		vec2 SpawnPos;
		if(GameServer()->m_pController->CanSpawn(m_pPlayer->GetTeam(), &SpawnPos))
		{
			m_Core.m_HookedPlayer = -1;
			m_Core.m_HookState = HOOK_RETRACTED;
			m_Core.m_TriggeredEvents |= COREEVENT_HOOK_RETRACT;
			m_Core.m_HookState = HOOK_RETRACTED;
			m_Core.m_Pos = SpawnPos;
			m_Core.m_HookPos = m_Core.m_Pos;
		}
		return;
	}
}

void CCharacter::DDRaceTick()
{
	m_Armor=(m_FreezeTime >= 0)?10-(m_FreezeTime/15):0;
    if(m_Input.m_Direction != 0 || m_Input.m_Jump != 0)
        m_LastMove = Server()->Tick();

    if(m_FreezeTime > 0 || m_FreezeTime == -1)
    {
        if (m_FreezeTime % Server()->TickSpeed() == 0 || m_FreezeTime == -1)
        {
            // звезды от фриза
            GameServer()->CreateDamageInd(m_Pos, 0, m_FreezeTime / Server()->TickSpeed(), Teams()->TeamMask(Team(), -1, m_pPlayer->GetCID()));
        }
        if(m_FreezeTime > 0)
            m_FreezeTime--;
        // else
            // m_Ninja.m_ActivationTick = Server()->Tick();


        m_Input.m_Direction = 0;
        m_Input.m_Jump = 0;
        m_Input.m_Hook = 0;
        m_PlAs_Direction = 0;
        m_PlAs_Jump = 0;
        m_PlAs_Hook = 0;
        if (m_FreezeTime == 1) {
            UnFreeze();
        }
    }
    
    m_Core.m_Id = GetPlayer()->GetCID();

/*
    // something to chat ~ 1s
    int64 Now = time_get();
    if(Now % 10 == 0) {
        char aBuf[64];
        str_format(aBuf, sizeof(aBuf), "   %d    ", m_Input.m_Direction);
        GameServer()->SendChatTarget(m_pPlayer->GetCID(), aBuf);
    }
*/
}


void CCharacter::DDRacePostCoreTick()
{
	if (m_pPlayer->m_DefEmoteReset >= 0 && m_pPlayer->m_DefEmoteReset <= Server()->Tick())
		{
		m_pPlayer->m_DefEmoteReset = -1;
			m_EmoteType = m_pPlayer->m_DefEmote = EMOTE_NORMAL;
			m_EmoteStop = -1;
		}

		if (m_EndlessHook || (m_Super && g_Config.m_SvEndlessSuperHook))
			m_Core.m_HookTick = 0;

		if (m_DeepFreeze && !m_Super)
			Freeze();

		if (m_Super && m_Core.m_Jumped > 1)
			m_Core.m_Jumped = 1;

		int CurrentIndex = GameServer()->Collision()->GetMapIndex(m_Pos);
		HandleSkippableTiles(CurrentIndex);

		// handle Anti-Skip tiles
		std::list < int > Indices = GameServer()->Collision()->GetMapIndices(m_PrevPos, m_Pos);
		if(!Indices.empty())
		{
			// iDDRace64
			int DummyID = m_pPlayer->m_DummyID; //if character is dummy, DummyID contains id of his owner
			if(DummyID >=0)
			{
				CCharacter* pDummyChar = GameServer()->GetPlayerChar(DummyID);
				//this huge condition blocks handling tiles between owner and dummy on /dc
				if((GetPlayer()->m_IsDummy || GetPlayer()->m_HasDummy) &&
					GameServer()->m_apPlayers[DummyID] && pDummyChar &&
					!(pDummyChar->m_Pos == m_Pos) &&
					(pDummyChar->m_PrevPos == m_Pos || m_PrevPos == pDummyChar->m_Pos))
					return;
			}
			for(std::list < int >::iterator i = Indices.begin(); i != Indices.end(); i++)
			{
				HandleTiles(*i);
				//dbg_msg("Running","%d", *i);
			}
		}
		else
		{
			HandleTiles(CurrentIndex);
			m_LastIndexTile = 0;
			m_LastIndexFrontTile = 0;
			//dbg_msg("Running","%d", CurrentIndex);
		}

		HandleBroadcast();
}

bool CCharacter::Freeze(int Seconds)
{
	if ((Seconds <= 0 || m_Super || m_FreezeTime == -1 || m_FreezeTime > Seconds * Server()->TickSpeed()) && Seconds != -1)
		 return false;
	if (m_FreezeTick < Server()->Tick() - Server()->TickSpeed() || Seconds == -1)
	{
		for(int i = 0; i < NUM_WEAPONS; i++)
			if(m_aWeapons[i].m_Got)
			 {
				 m_aWeapons[i].m_Ammo = 0;
			 }
		m_Armor = 0;
		m_FreezeTime = Seconds == -1 ? Seconds : Seconds * Server()->TickSpeed();
		m_FreezeTick = Server()->Tick();
		return true;
	}
	return false;
}
bool CCharacter::DeephFreeze() {
    if(m_Super)
        m_Super = false;
	return m_DeepFreeze = true;
}
bool CCharacter::unDeephFreeze()
{
    return m_DeepFreeze = false;
}
bool CCharacter::Freeze()
{
	return Freeze(g_Config.m_SvFreezeDelay);
}

bool CCharacter::UnFreeze()
{
	if (m_FreezeTime > 0)
	{
		m_Armor=10;
		for(int i=0;i<NUM_WEAPONS;i++)
			if(m_aWeapons[i].m_Got)
			 {
				 m_aWeapons[i].m_Ammo = -1;
			 }
		if(!m_aWeapons[m_ActiveWeapon].m_Got)
			m_ActiveWeapon = WEAPON_GUN;
		m_FreezeTime = 0;
		m_FreezeTick = 0;
		if (m_ActiveWeapon==WEAPON_HAMMER) m_ReloadTimer = 0;
		 return true;
	}
	return false;
}

void CCharacter::GiveAllWeapons()
{
	 for(int i=1;i<NUM_WEAPONS-1;i++)
	 {
		 m_aWeapons[i].m_Got = true;
		 if(!m_FreezeTime) m_aWeapons[i].m_Ammo = -1;
	 }
	 return;
}

void CCharacter::Pause(bool Pause)
{
	m_Paused = Pause;
	if(Pause)
	{
		GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = 0;
		GameServer()->m_World.RemoveEntity(this);
	}
	else
	{
		m_Core.m_Vel = vec2(0,0);
		GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = &m_Core;
		GameServer()->m_World.InsertEntity(this);
	}
}

void CCharacter::DDRaceInit()
{

	m_Paused = false;
	m_DDRaceState = DDRACE_NONE;
	m_PrevPos = m_Pos;
	m_LastBroadcast = 0;
	m_TeamBeforeSuper = 0;
	m_Core.m_Id = GetPlayer()->GetCID();
	if(GetPlayer()->m_IsUsingDDRaceClient) ((CGameControllerDDRace*)GameServer()->m_pController)->m_Teams.SendTeamsState(GetPlayer()->GetCID());
	if(g_Config.m_SvTeam == 2)
	{
		GameServer()->SendChatTarget(GetPlayer()->GetCID(),"Please join a team before you start");
		m_LastStartWarning = Server()->Tick();
	}
	m_TeleCheckpoint = 0;
	m_EndlessHook = g_Config.m_SvEndlessDrag;
	m_Hit = g_Config.m_SvHit ? HIT_ALL : DISABLE_HIT_GRENADE|DISABLE_HIT_HAMMER|DISABLE_HIT_RIFLE|DISABLE_HIT_SHOTGUN;
}

// iDDRace64
void CCharacter::iDDRaceInit()
{
	m_LastIndexTile = 0;
	m_LastIndexFrontTile = 0;
	m_FastReload = false;
	m_ReloadMultiplier = 1000;
	m_DoHammerFly = HF_NONE;
	m_RescueUnfreeze = 0;
    m_LastRescue = 0;
	m_SavedPos = vec2(0, 0);
}
void CCharacter::iDDRaceTick()
{
	SavePos();
	RescueUnfreeze();

	HandleRainbow();
	HandleBlood();
    HandleJumps();

    isCanDie = m_pPlayer->isCanDiePl ? true : false;
    isCanKill = m_pPlayer->isCanKillPl ? true : false;

    // HandleRescue();
	// for dummy only
    // if(GetPlayer()->m_IsDummy)
        // CrazyDummy();
	if(!GetPlayer()->m_IsDummy)
		return;
	if(!GetPlayer()->m_DummyCopiesMove && (!m_DoHammerFly || m_DoHammerFly==HF_NONE))
	{
		if(GetActiveWeapon() == WEAPON_HAMMER) SetActiveWeapon(WEAPON_GUN);
		ResetDummy();
	}
	if(m_DoHammerFly > HF_NONE)
		DoHammerFly();


    if(m_LastRescue > 0) m_LastRescue--;
    if(m_LastRescueSave > 0) m_LastRescueSave--;
}
void CCharacter::XXLDDRaceInit()
{
    m_THammer = false;
    m_EHammer = false;
	m_DeepHammer= false;
    m_unDeepHammer= false;
	m_CheckMove = false;
	m_IceHammer = false;
	m_FastReload = false;
	m_ReloadMultiplier = 1000;
	m_LastIndexTile = 0;
	m_LastIndexFrontTile = 0;
	m_Fly = true;
	m_Bloody = 0;
    m_gBounce = false;
}
void CCharacter::HandleBlood()
{
	if (m_Bloody){
		GameServer()->CreateDeath(m_Pos, m_pPlayer->GetCID(), Teams()->TeamMask(Team()));
	}

}
void CCharacter::SavePos()
{
    if(g_Config.m_SvNewRescue) {
        // Баг с /r
        int MapIndexRescueR = GameServer()->Collision()->GetPureMapIndex(vec2(m_Pos.x + 2, m_Pos.y));
        int MapIndexRescueL = GameServer()->Collision()->GetPureMapIndex(vec2(m_Pos.x - 2, m_Pos.y));
        int m_TileIndexRescueOffsetR = GameServer()->Collision()->GetTileIndex(MapIndexRescueR);
        int m_TileFIndexRescueOffsetR = GameServer()->Collision()->GetFTileIndex(MapIndexRescueR);
        int m_TileIndexRescueOffsetL = GameServer()->Collision()->GetTileIndex(MapIndexRescueL);
        int m_TileFIndexRescueOffsetL = GameServer()->Collision()->GetFTileIndex(MapIndexRescueL);
        if(g_Config.m_SvRescue && m_Pos) {
            if(!m_FreezeTime
                    && IsGrounded()
                    && m_TileIndexRescueOffsetR != TILE_FREEZE
                    && m_TileFIndexRescueOffsetR != TILE_FREEZE
                    && m_TileIndexRescueOffsetL != TILE_FREEZE
                    && m_TileFIndexRescueOffsetL != TILE_FREEZE
                    && m_RescueUnfreeze == 0
                    && m_TileIndex != TILE_FREEZE
                    && m_TileFIndex != TILE_FREEZE
            ) {
            	m_SavedPos = m_Pos;
            }
	   }
    }
    else {
        if(g_Config.m_SvRescue && m_Pos) {
            if(!m_FreezeTime && IsGrounded() && m_Pos==m_PrevPos && m_RescueUnfreeze == 0 && m_TileIndex != TILE_FREEZE && m_TileFIndex != TILE_FREEZE) {
                m_SavedPos=m_Pos;
            }
        }
    }
}
void CCharacter::RescueUnfreeze()
{
	if (m_RescueUnfreeze == 2)
	{
		m_RescueUnfreeze = 0;
		UnFreeze();
	}
	if (m_RescueUnfreeze == 1)
		m_RescueUnfreeze = 2;
}
void CCharacter::Rescue() //for Learath2
{
    if(m_DeepFreeze) {
        GameServer()->SendChatTarget(GetPlayer()->GetCID(),"You can't use rescue while you are deepfreezed");
        return;
    }
	if(m_SavedPos && m_FreezeTime && !(m_SavedPos== vec2(0,0)) && m_FreezeTime!=0)
	{
			m_PrevPos = m_SavedPos;//TIGROW edit
            if(g_Config.m_SvEffects) {
                // Blood effect
                GameServer()->CreateDeath(
                    Core()->m_Pos, // position (also m_SavedPos)
                    m_pPlayer->GetCID(), // clientid
                    Teams()->TeamMask(Team(), -1, m_pPlayer->GetCID()) // team important!!
                );
                // Spawn effect
                /*GameServer()->CreatePlayerSpawn(
                    Core()->m_Pos,
                    Teams()->TeamMask(Team(), -1, m_pPlayer->GetCID())
                );*/
            }
            if(!g_Config.m_SvSaveImpulse) {
                m_Core.m_Vel = vec2(0,0);
            }
            Core()->m_Pos = m_SavedPos;
            m_RescueUnfreeze = 1;
			UnFreeze();
	}
	else if(!m_SavedPos ||  m_SavedPos== vec2(0,0))
		GameServer()->SendChatTarget(GetPlayer()->GetCID(),"No position saved since start");
    	// else if (!GetPlayer()->m_IsDummy)
    	// 	GameServer()->SendChatTarget(GetPlayer()->GetCID(),"You are not freezed");
}
void CCharacter::HandleRescue()
{

    // calculate offset tiles because of random position saving at m_Pos
    int MapIndexRescueR = GameServer()->Collision()->GetPureMapIndex(vec2(m_Pos.x + 2, m_Pos.y));
    int MapIndexRescueL = GameServer()->Collision()->GetPureMapIndex(vec2(m_Pos.x - 2, m_Pos.y));

    int m_TileIndexRescueOffsetR = GameServer()->Collision()->GetTileIndex(MapIndexRescueR);
    int m_TileFIndexRescueOffsetR = GameServer()->Collision()->GetFTileIndex(MapIndexRescueR);
    int m_TileIndexRescueOffsetL = GameServer()->Collision()->GetTileIndex(MapIndexRescueL);
    int m_TileFIndexRescueOffsetL = GameServer()->Collision()->GetFTileIndex(MapIndexRescueL);

    // check if the rescue pos should be saved
    if (!m_LastRescueSave
            && !m_FreezeTime
            && !m_DeepFreeze
            && m_TileIndexRescueOffsetR != TILE_FREEZE
            && m_TileFIndexRescueOffsetR != TILE_FREEZE
            && m_TileIndexRescueOffsetL != TILE_FREEZE
            && m_TileFIndexRescueOffsetL != TILE_FREEZE
            // nearly the same like IsGrounded(), but with less tolerance
            && (GameServer()->Collision()->CheckPoint(m_Pos.x+m_ProximityRadius/2, m_Pos.y+m_ProximityRadius/2+1) || GameServer()->Collision()->CheckPoint(m_Pos.x-m_ProximityRadius/2, m_Pos.y+m_ProximityRadius/2+1)))
    {
        m_RescuePos = m_Pos;
        m_LastRescueSave = 1; // not every point will be stored
    }
}
void CCharacter::ResetSavedPos()
{
	m_SavedPos = vec2(0,0);
}
void CCharacter::ResetDummy()
{
    m_Input.m_TargetX = 100;
    m_Input.m_TargetY = 0;
    m_Input.m_Fire = 0;
    m_LatestInput.m_Fire = 0;
}
void CCharacter::CrazyDummy()
{
    // vec2 Direction = normalize(vec2(m_Input.m_TargetX, m_Input.m_TargetY));
    // for (int i = -100; i < 100; ++i)
    // {
        // m_Input.m_TargetX = i;
    // }
    // for (int i = -10; i < 10; ++i)
    // {
        // m_Input.m_TargetY = i;
    // }
}
void CCharacter::DoHammerFly()
{
	if(GetPlayer()->m_DummyCopiesMove) //under control
		return;
	if (GetActiveWeapon() != WEAPON_HAMMER)
		SetActiveWeapon(WEAPON_HAMMER);
	if(m_DoHammerFly==HF_VERTICAL)
	{
		m_LatestInput.m_TargetX = 0; //look up
		m_LatestInput.m_TargetY = -100;
		m_Input.m_TargetX = 0;
		m_Input.m_TargetY = -100;
		m_Input.m_Fire = 1;
		m_LatestInput.m_Fire = 1;
	}
	else if(m_DoHammerFly == HF_HORIZONTAL)
	{
		//the character of dummy's owner, we put owner's ID to dummy's CPlayer::m_DummyID when ran chat cmd
		CCharacter* pOwnerChr = GameServer()->m_apPlayers[GetPlayer()->m_DummyID]->GetCharacter();
		if(!pOwnerChr) return;
		//final target pos
		vec2 AimPos = pOwnerChr->m_Pos - m_Pos;

		//follow owner
		m_LatestInput.m_TargetX = AimPos.x;
		m_LatestInput.m_TargetY = AimPos.y;
		m_Input.m_TargetX = AimPos.x;
		m_Input.m_TargetY = AimPos.y;

		//no need in shoot if we are not under the owner or owner is far away
		if (m_Pos.y <= pOwnerChr->m_Pos.y || distance(m_Pos, pOwnerChr->m_Pos)>100)
		{
			m_Input.m_Fire = 0;
			m_LatestInput.m_Fire = 0;
			return;
		}

		//also dummy should hammer on touch, so that's what TODO next

		m_Input.m_Fire = 1;
		m_LatestInput.m_Fire = 1;
	}
}
void CCharacter::HandleRainbow()
{
	if (m_pPlayer->m_Rainbow == RAINBOW_COLOR)
	{
		m_pPlayer->m_TeeInfos.m_UseCustomColor = 1;

		if (m_pPlayer->m_LastRainbow >= 16711424 || m_pPlayer->m_LastRainbow < 65280 )
			m_pPlayer->m_LastRainbow = 65280;
		else
			m_pPlayer->m_LastRainbow += 65536;  //the magic number

		// m_pPlayer->m_TeeInfos.m_ColorFeet = m_pPlayer->m_LastRainbow;

		if (m_pPlayer->m_Rainbow == RAINBOW_COLOR)
			m_pPlayer->m_TeeInfos.m_ColorBody = m_pPlayer->m_LastRainbow;
	}
    else if (m_pPlayer->m_Rainbow == RAINBOW_BLACKWHITE)
    {
        m_pPlayer->m_TeeInfos.m_UseCustomColor = 1;
        if (m_pPlayer->m_LastRainbow > 255)
            m_pPlayer->m_LastRainbow = 0;
        else if (m_pPlayer->m_LastRainbow == 0)
            m_pPlayer->m_RainbowBwUp = false;
        else if (m_pPlayer->m_LastRainbow == 255)
            m_pPlayer->m_RainbowBwUp = true;

        if (m_pPlayer->m_RainbowBwUp)
            m_pPlayer->m_LastRainbow-=1;
        else
            m_pPlayer->m_LastRainbow+=1;

        m_pPlayer->m_TeeInfos.m_ColorBody = m_pPlayer->m_LastRainbow;
        // m_pPlayer->m_TeeInfos.m_ColorFeet = m_pPlayer->m_LastRainbow;
    }
    else {}
    if (g_Config.m_SvFainbowFeet && m_pPlayer->m_RainbowFeet) {
        m_pPlayer->m_TeeInfos.m_UseCustomColor = 1;

        if (m_pPlayer->m_LastRainbow >= 16711424 || m_pPlayer->m_LastRainbow < 65280)
            m_pPlayer->m_LastRainbow = 65280;
        else
            m_pPlayer->m_LastRainbow += 65536;  //the magic number

        m_pPlayer->m_TeeInfos.m_ColorFeet = m_pPlayer->m_LastRainbow;

        // if (m_pPlayer->m_Rainbow == RAINBOW_COLOR)
            // m_pPlayer->m_TeeInfos.m_ColorBody = m_pPlayer->m_LastRainbow;
    }
    else if (!m_pPlayer->m_RainbowFeet) {
        m_pPlayer->m_TeeInfos.m_UseCustomColor = 1;

        if (m_pPlayer->m_LastRainbow >= 16711424 || m_pPlayer->m_LastRainbow < 65280)
            m_pPlayer->m_LastRainbow = 65280;
        else
            m_pPlayer->m_LastRainbow += 65536;

        m_pPlayer->m_TeeInfos.m_ColorFeet = 8519679;
    }

}
void CCharacter::HandleJumps()
{
    if (m_Core.m_Jumped > 1 && m_Core.m_MaxJumps > m_Core.m_JumpCount + 2)
    {
        m_Core.m_Jumped = 1;
        m_Core.m_JumpCount++;
    }
    else if (m_Core.m_MaxJumps == 1)
        m_Core.m_Jumped = 2; //1 Jump
    else if (m_Core.m_MaxJumps == 0)
        m_Core.m_Jumped = 1; //0 Jumps
}
void CCharacter::Save()
{
    if(m_TileIndex == TILE_END || m_TileFIndex == TILE_END) {
        char aBuf[64];
        str_format(aBuf, sizeof(aBuf), "You can't save your position");
        GameServer()->SendChatTarget(m_pPlayer->GetCID(), aBuf);
        return;
    }
    else {
        m_PosLas = m_Pos;
        // char aBuf[256];
        // str_format(aBuf, sizeof(aBuf), "Position saved");
        // GameServer()->SendChatTarget(m_pPlayer->GetCID(), aBuf);

    }
}
void CCharacter::Load()
{
    if(m_PosLas == vec2 (0,0)) {
        char aBuf[256];
        str_format(aBuf, sizeof(aBuf), "No one saved position");
        GameServer()->SendChatTarget(m_pPlayer->GetCID(), aBuf);
    }
    else {
        Core()->m_Pos = m_PosLas;
        // char aBuf[256];
        // str_format(aBuf, sizeof(aBuf), "%d Position Loaded",m_PosLas);
        // GameServer()->SendChatTarget(m_pPlayer->GetCID(), aBuf);
    }
}
int CCharacter::ClnId() {
   return m_pPlayer->GetCID();
}
float CCharacter::GetFloatAngleAbs(int Value) {
    int Bullet = Value;
    if(!Bullet) {
        return 0;
    }
    if(Bullet <= 0) {
        return 0;
    }
    if(Bullet >= 20) {
        Bullet = 20;
    }
    if(Bullet%2 == 0) {
        return -(Bullet/2-1);
    }
    else {
        return -(Bullet/2);
    }
}

float CCharacter::GetFloatAngle(int Value) {
    int Bullet = Value;
    if(!Bullet) {
        return 0;
    }
    if(Bullet <= 0) {
        return 0;
    }
    if(Bullet >= 20) {
        Bullet = 20;
    }
    if(Bullet%2 == 0) {
        return Bullet/2;
    }
    else {
        return (Bullet/2);
    }
}
// char aBuf[256];
// str_format(aBuf, sizeof(aBuf), "Count of bullets %d",Bullet);
// GameServer()->SendChatTarget(m_pPlayer->GetCID(), aBuf);

