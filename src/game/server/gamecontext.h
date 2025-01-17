/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_GAMECONTEXT_H
#define GAME_SERVER_GAMECONTEXT_H

#include <engine/server.h>
#include <engine/console.h>
#include <engine/shared/memheap.h>

#include <game/layers.h>
#include <game/voting.h>

#include "eventhandler.h"
#include "gamecontroller.h"
#include "gameworld.h"
#include "player.h"

#include "score.h"
#ifdef _MSC_VER
typedef __int32 int32_t;
typedef unsigned __int32 uint32_t;
typedef __int64 int64_t;
typedef unsigned __int64 uint64_t;
#else
#include <stdint.h>
#endif
/*
	Tick
		Game Context (CGameContext::tick)
			Game World (GAMEWORLD::tick)
				Reset world if requested (GAMEWORLD::reset)
				All entities in the world (ENTITY::tick)
				All entities in the world (ENTITY::tick_defered)
				Remove entities marked for deletion (GAMEWORLD::remove_entities)
			Game Controller (GAMECONTROLLER::tick)
			All players (CPlayer::tick)


	Snap
		Game Context (CGameContext::snap)
			Game World (GAMEWORLD::snap)
				All entities in the world (ENTITY::snap)
			Game Controller (GAMECONTROLLER::snap)
			Events handler (EVENT_HANDLER::snap)
			All players (CPlayer::snap)

*/
class CGameContext : public IGameServer
{
	IServer *m_pServer;
	class IConsole *m_pConsole;
	CLayers m_Layers;
	CCollision m_Collision;
	CNetObjHandler m_NetObjHandler;


	static void ConTuneParam(IConsole::IResult *pResult, void *pUserData);
	static void ConTuneReset(IConsole::IResult *pResult, void *pUserData);
	static void ConTuneDump(IConsole::IResult *pResult, void *pUserData);
	static void ConPause(IConsole::IResult *pResult, void *pUserData);
	static void ConChangeMap(IConsole::IResult *pResult, void *pUserData);
	static void ConRestart(IConsole::IResult *pResult, void *pUserData);
	static void ConBroadcast(IConsole::IResult *pResult, void *pUserData);
	static void ConSay(IConsole::IResult *pResult, void *pUserData);
	static void ConSetTeam(IConsole::IResult *pResult, void *pUserData);
	static void ConSetTeamAll(IConsole::IResult *pResult, void *pUserData);
	//static void ConSwapTeams(IConsole::IResult *pResult, void *pUserData);
	//static void ConShuffleTeams(IConsole::IResult *pResult, void *pUserData);
	//static void ConLockTeams(IConsole::IResult *pResult, void *pUserData);
	static void ConAddVote(IConsole::IResult *pResult, void *pUserData);
	static void ConRemoveVote(IConsole::IResult *pResult, void *pUserData);
	static void ConForceVote(IConsole::IResult *pResult, void *pUserData);
	static void ConClearVotes(IConsole::IResult *pResult, void *pUserData);
	static void ConVote(IConsole::IResult *pResult, void *pUserData);
	static void ConchainSpecialMotdupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);

	CGameContext(int Resetting);
	void Construct(int Resetting);

	bool m_Resetting;


public:
    CTuningParams m_Tuning;
	IServer *Server() const { return m_pServer; }
	class IConsole *Console() { return m_pConsole; }
	CCollision *Collision() { return &m_Collision; }
	CTuningParams *Tuning() { return &m_Tuning; }

	CGameContext();
	~CGameContext();

	void Clear();

	CEventHandler m_Events;
	CPlayer *m_apPlayers[MAX_CLIENTS];

	IGameController *m_pController;
	CGameWorld m_World;

	// helper functions
	class CCharacter *GetPlayerChar(int ClientID);

	//int m_LockTeams;

	// voting
	void StartVote(const char *pDesc, const char *pCommand, const char *pReason);
	void EndVote();
	void SendVoteSet(int ClientID);
	void SendVoteStatus(int ClientID, int Total, int Yes, int No);
	void AbortVoteKickOnDisconnect(int ClientID);

	int m_VoteCreator;
	int64 m_VoteCloseTime;
	bool m_VoteUpdate;
	int m_VotePos;
	char m_aVoteDescription[VOTE_DESC_LENGTH];
	char m_aVoteCommand[VOTE_CMD_LENGTH];
	char m_aVoteReason[VOTE_REASON_LENGTH];
	int m_NumVoteOptions;
	int m_VoteEnforce;
	enum
	{
		VOTE_ENFORCE_UNKNOWN=0,
		VOTE_ENFORCE_NO,
		VOTE_ENFORCE_YES,
	};
	CHeap *m_pVoteOptionHeap;
	CVoteOptionServer *m_pVoteOptionFirst;
	CVoteOptionServer *m_pVoteOptionLast;

	// helper functions
	void CreateDamageInd(vec2 Pos, float AngleMod, int Amount, int64_t Mask=-1);
	void CreateExplosion(vec2 Pos, int Owner, int Weapon, bool NoDamage, int ActivatedTeam, int64_t Mask=-1);
	void CreateHammerHit(vec2 Pos, int64_t Mask=-1);
	void CreatePlayerSpawn(vec2 Pos, int64_t Mask=-1);
	void CreateDeath(vec2 Pos, int Who, int64_t Mask=-1);
	void CreateSound(vec2 Pos, int Sound, int64_t Mask=-1);
	void CreateSoundGlobal(int Sound, int Target=-1);


	enum
	{
		CHAT_ALL=-2,
		CHAT_SPEC=-1,
		CHAT_RED=0,
		CHAT_BLUE=1
	};

	// network
	void SendChatTarget(int To, const char *pText);
	void SendChat(int ClientID, int Team, const char *pText, int SpamProtectionClientID = -1);
	void SendEmoticon(int ClientID, int Emoticon);
	void SendWeaponPickup(int ClientID, int Weapon);
	void SendBroadcast(const char *pText, int ClientID);

	// iDDRace64
	void NewDummy(int DummyID, bool CustomColor = false, int ColorBody = 12895054, int ColorFeet = 12895054, const char *pSkin = "coala", const char *pName = "Dummy", const char *pClan = "[16x16]", int Country = -1);

	void List(int ClientID, const char* filter);

	//
	void CheckPureTuning();
	void SendTuningParams(int ClientID);

	//
	//void SwapTeams();

	// engine events
	virtual void OnInit();
	virtual void OnConsoleInit();
	virtual void OnShutdown();

	virtual void OnTick();
	virtual void OnPreSnap();
	virtual void OnSnap(int ClientID);
	virtual void OnPostSnap();

	virtual void OnMessage(int MsgID, CUnpacker *pUnpacker, int ClientID);

	virtual void OnClientConnected(int ClientID);
	virtual void OnClientEnter(int ClientID);
	virtual void OnClientDrop(int ClientID, const char *pReason);
	virtual void OnClientDirectInput(int ClientID, void *pInput);
	virtual void OnClientPredictedInput(int ClientID, void *pInput);

	virtual bool IsClientReady(int ClientID);
	virtual bool IsClientPlayer(int ClientID);

	virtual const char *GameType();
	virtual const char *Version();
	virtual const char *NetVersion();

	// DDRace

	int ProcessSpamProtection(int ClientID);
	int GetDDRaceTeam(int ClientID);

private:

	bool m_VoteWillPass;
	class IScore *m_pScore;



	// DDRace Console Commands
    static void ConKillPlayer(IConsole::IResult *pResult, void *pUserData);
    static void ConTeleport(IConsole::IResult *pResult, void *pUserData);

    static void ConWeapons(IConsole::IResult *pResult, void *pUserData);
    static void ConAddWeapon(IConsole::IResult *pResult, void *pUserData);
    static void ConUnWeapons(IConsole::IResult *pResult, void *pUserData);
    static void ConRemoveWeapon(IConsole::IResult *pResult, void *pUserData);

    static void ConShotgun(IConsole::IResult *pResult, void *pUserData);
    static void ConGrenade(IConsole::IResult *pResult, void *pUserData);
    static void ConLaser(IConsole::IResult *pResult, void *pUserData);

    static void ConUnShotgun(IConsole::IResult *pResult, void *pUserData);
    static void ConUnGrenade(IConsole::IResult *pResult, void *pUserData);
    static void ConUnLaser(IConsole::IResult *pResult, void *pUserData);
    static void ConNinja(IConsole::IResult *pResult, void *pUserData);

    static void ConSuper(IConsole::IResult *pResult, void *pUserData);
    static void ConUnSuper(IConsole::IResult *pResult, void *pUserData);

    static void ConGoLeft(IConsole::IResult *pResult, void *pUserData);
    static void ConGoRight(IConsole::IResult *pResult, void *pUserData);
    static void ConGoUp(IConsole::IResult *pResult, void *pUserData);
    static void ConGoDown(IConsole::IResult *pResult, void *pUserData);

    static void ConMove(IConsole::IResult *pResult, void *pUserData);
    static void ConMoveRaw(IConsole::IResult *pResult, void *pUserData);
    static void ConForcePause(IConsole::IResult *pResult, void *pUserData);
    static void ConShowOthers(IConsole::IResult *pResult, void *pUserData);

    static void ConList(IConsole::IResult *pResult, void *pUserData);

    static void ConSetJumps(IConsole::IResult *pResult, void *pUserData);
    static void ConBlood(IConsole::IResult *pResult, void *pUserData);
    static void ConToggleFly(IConsole::IResult *pResult, void *pUserData);

    static void ConFastReload(IConsole::IResult *pResult, void *pUserData);

    static void ConUnFastReload(IConsole::IResult *pResult, void *pUserData);
    static void ConRainbow(IConsole::IResult *pResult, void *pUserData);

    static void ConInvis(IConsole::IResult *pResult, void *pUserData);
    static void ConVis(IConsole::IResult *pResult, void *pUserData);

    static void ConWhisper(IConsole::IResult *pResult, void *pUserData);
    static void ConRename(IConsole::IResult *pResult, void *pUserData);
    static void ConScore(IConsole::IResult *pResult, void *pUserData);

    static void ConHammer(IConsole::IResult *pResult, void *pUserData);
    static void ConTHammer(IConsole::IResult *pResult, void *pUserData);
    static void ConEHammer(IConsole::IResult *pResult, void *pUserData);
    static void ConHeXP(IConsole::IResult *pResult, void *pUserData);
    static void ConGuneXP(IConsole::IResult *pResult, void *pUserData);

    static void ConGrenadeBounce(IConsole::IResult *pResult, void *pUserData);
    static void ConShotgunWall(IConsole::IResult *pResult, void *pUserData);

    static void ConIceHammer(IConsole::IResult *pResult, void *pUserData);
    static void ConUnIceHammer(IConsole::IResult *pResult, void *pUserData);

    static void ConDeepHammer(IConsole::IResult *pResult, void *pUserData);
    static void ConUnDeepHammer(IConsole::IResult *pResult, void *pUserData);

    static void ConGHammer(IConsole::IResult *pResult, void *pUserData);
    static void ConUnGHammer(IConsole::IResult *pResult, void *pUserData);

    static void ConColorHammer(IConsole::IResult *pResult, void *pUserData);
    static void ConUnColorHammer(IConsole::IResult *pResult, void *pUserData);

    static void ConPlasmaLaser(IConsole::IResult *pResult, void *pUserData);
    static void ConUnPlasmaLaser(IConsole::IResult *pResult, void *pUserData);

    static void ConGunSpread(IConsole::IResult *pResult, void *pUserData);
    static void ConShotgunSpread(IConsole::IResult *pResult, void *pUserData);
    static void ConGrenadeSpread(IConsole::IResult *pResult, void *pUserData);
    static void ConLaserSpread(IConsole::IResult *pResult, void *pUserData);

    static void ConGiveKid(IConsole::IResult *pResult, void *pUserData);
    static void ConGiveHelper(IConsole::IResult *pResult, void *pUserData);
    static void ConGiveModer(IConsole::IResult *pResult, void *pUserData);

    static void ConRemoveLevel(IConsole::IResult *pResult, void *pUserData);

    static void ConFreezePlayer(IConsole::IResult *pResult, void *pUserData);
    static void ConUnFreezePlayer(IConsole::IResult *pResult, void *pUserData);

    static void ConPlayAs(IConsole::IResult *pResult, void *pUserData);
    
    static void ConCanKill(IConsole::IResult *pResult, void *pUserData);
    static void ConCanDie(IConsole::IResult *pResult, void *pUserData);

    static void ConId(IConsole::IResult *pResult, void *pUserData);
    static void ConRemRise(IConsole::IResult *pResult, void *pUserData);
    static void ConRainbowMe(IConsole::IResult *pResult, void *pUserData);
    static void ConRainbowFeetMe(IConsole::IResult *pResult, void *pUserData);



    static void ConMute(IConsole::IResult *pResult, void *pUserData);
    static void ConMuteID(IConsole::IResult *pResult, void *pUserData);
    static void ConMuteIP(IConsole::IResult *pResult, void *pUserData);
    static void ConUnmute(IConsole::IResult *pResult, void *pUserData);
    static void ConMutes(IConsole::IResult *pResult, void *pUserData);










    // chat commands
    static void ConCredits(IConsole::IResult *pResult, void *pUserData);
    static void ConEyeEmote(IConsole::IResult *pResult, void *pUserData);
    static void ConSetEyeEmote(IConsole::IResult *pResult, void *pUserData);
    static void ConSettings(IConsole::IResult *pResult, void *pUserData);
    static void ConHelp(IConsole::IResult *pResult, void *pUserData);
    static void ConInfo(IConsole::IResult *pResult, void *pUserData);
    static void ConMe(IConsole::IResult *pResult, void *pUserData);
    static void ConTogglePause(IConsole::IResult *pResult, void *pUserData);
    static void ConToggleSpec(IConsole::IResult *pResult, void *pUserData);
    static void ConRank(IConsole::IResult *pResult, void *pUserData);
    static void ConRules(IConsole::IResult *pResult, void *pUserData);
    static void ConJoinTeam(IConsole::IResult *pResult, void *pUserData);
    static void ConTop5(IConsole::IResult *pResult, void *pUserData);
    // conShowOthers see console commands
    static void ConSayTime(IConsole::IResult *pResult, void *pUserData);
    static void ConSayTimeAll(IConsole::IResult *pResult, void *pUserData);
    static void ConTime(IConsole::IResult *pResult, void *pUserData);
    static void ConSetTimerType(IConsole::IResult *pResult, void *pUserData);
    static void ConJumps(IConsole::IResult *pResult, void *pUserData);

    #if defined(CONF_SQL)
        static void ConTimes(IConsole::IResult *pResult, void *pUserData);
    #endif



    // iDDRace64
    static void ConDummy(IConsole::IResult *pResult, void *pUserData);
    static void ConDummyDelete(IConsole::IResult *pResult, void *pUserData);
    static void ConDummyChange(IConsole::IResult *pResult, void *pUserData);
    static void ConDummyHammer(IConsole::IResult *pResult, void *pUserData);
    static void ConDummyHammerFly(IConsole::IResult *pResult, void *pUserData);
    static void ConDummyControl(IConsole::IResult *pResult, void *pUserData);
    static void ConDummyCopyMove(IConsole::IResult *pResult, void *pUserData);


    static void ConRescue(IConsole::IResult *pResult, void *pUserData);

    static void ConSave(IConsole::IResult *pResult, void *pUserData);
    static void ConLoad(IConsole::IResult *pResult, void *pUserData);
    static void ConStop(IConsole::IResult *pResult, void *pUserData);
    static void ConSolo(IConsole::IResult *pResult, void *pUserData);
    static void ConUnFreeze(IConsole::IResult *pResult, void *pUserData);


    // Another
    static void ConKill(IConsole::IResult *pResult, void *pUserData);
    static void ConBroadTime(IConsole::IResult *pResult, void *pUserData);
    static void ConToggleBroadcast(IConsole::IResult *pResult, void *pUserData);
    void ModifyWeapons(IConsole::IResult *pResult, void *pUserData, int Weapon, bool Remove);
    void MoveCharacter(int ClientID, int X, int Y, bool Raw = false);
    static void ConUTF8(IConsole::IResult *pResult, void *pUserData);

	enum
	{
		MAX_MUTES=32,
	};
	struct CMute
	{
		NETADDR m_Addr;
		int m_Expire;
	};

	CMute m_aMutes[MAX_MUTES];
	int m_NumMutes;
	void Mute(IConsole::IResult *pResult, NETADDR *Addr, int Secs, const char *pDisplayName);

public:
	CLayers *Layers() { return &m_Layers; }
	class IScore *Score() { return m_pScore; }
	bool m_VoteKick;
	enum
	{
		VOTE_ENFORCE_NO_ADMIN = VOTE_ENFORCE_YES + 1,
		VOTE_ENFORCE_YES_ADMIN
	};
	int m_VoteEnforcer;
	void SendRecord(int ClientID);
	static void SendChatResponse(const char *pLine, void *pUser);
	static void SendChatResponseAll(const char *pLine, void *pUser);
	virtual void OnSetAuthed(int ClientID,int Level);
	virtual bool PlayerCollision();
	virtual bool PlayerHooking();

	void ResetTuning();

	int m_ChatResponseTargetID;
	int m_ChatPrintCBIndex;
};

inline int64_t CmaskAll() { return -1LL; }
inline int64_t CmaskOne(int ClientID) { return 1LL<<ClientID; }
inline int64_t CmaskAllExceptOne(int ClientID) { return CmaskAll()^CmaskOne(ClientID); }
inline bool CmaskIsSet(int64_t Mask, int ClientID) { return (Mask&CmaskOne(ClientID)) != 0; }
#endif
