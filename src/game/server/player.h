/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_PLAYER_H
#define GAME_SERVER_PLAYER_H

// this include should perhaps be removed
#include "entities/character.h"
#include "gamecontext.h"
	//Rainbowstates
enum
{
	RAINBOW_NONE = 0,
	RAINBOW_COLOR,
	RAINBOW_BLACKWHITE
};
// player object
class CPlayer
{
	MACRO_ALLOC_POOL_ID()

public:
	CPlayer(CGameContext *pGameServer, int ClientID, int Team);
	~CPlayer();

	void Init(int CID);

	void TryRespawn();
	void Respawn();
	void SetTeam(int Team, bool DoChatMsg=true);
	int GetTeam() const { return m_Team; };
	int GetCID() const { return m_ClientID; };

	void Tick();
	void PostTick();
	void Snap(int SnappingClient);
	void FakeSnap(int SnappingClient);

	void OnDirectInput(CNetObj_PlayerInput *NewInput);
	void OnPredictedInput(CNetObj_PlayerInput *NewInput);
	void OnDisconnect(const char *pReason);

	void KillCharacter(int Weapon = WEAPON_GAME);
	CCharacter *GetCharacter();

	void FindDuplicateSkins();

	//---------------------------------------------------------
	// this is used for snapping so we know how we can clip the view for the player
	vec2 m_ViewPos;

	// states if the client is chatting, accessing a menu etc.
	int m_PlayerFlags;

	// used for snapping to just update latency if the scoreboard is active
	int m_aActLatency[MAX_CLIENTS];

	// used for spectator mode
	int m_SpectatorID;

	bool m_IsReady;

	//
	int m_Vote;
	int m_VotePos;
	//
	int m_LastVoteCall;
	int m_LastVoteTry;
	int m_LastChat;
	int m_LastSetTeam;
	int m_LastSetSpectatorMode;
	int m_LastChangeInfo;
	int m_LastEmote;
	int m_LastKill;

	// TODO: clean this up
	struct
	{
		char m_SkinName[64];
		int m_UseCustomColor;
		int m_ColorBody;
		int m_ColorFeet;
	} m_TeeInfos;

	int m_RespawnTick;
	int m_DieTick;
	int m_Score;
	int m_ScoreStartTick;
	bool m_ForceBalanced;
	int m_LastActionTick;
	bool m_StolenSkin;
	int m_TeamChangeTick;
	struct
	{
		int m_TargetX;
		int m_TargetY;
	} m_LatestActivity;

	// network latency calculations
	struct
	{
		int m_Accum;
		int m_AccumMin;
		int m_AccumMax;
		int m_Avg;
		int m_Min;
		int m_Max;
	} m_Latency;

    bool isCanDiePl;
    bool isCanKillPl;
    int m_Rainbow;
	bool m_RainbowFeet;
	int m_LastRainbow;
	bool m_RainbowBwUp;
	int m_Helped;
	char m_OrginalName[MAX_NAME_LENGTH];
	bool m_IsOrginalName;
	bool m_IsMember;
	bool m_IsLoggedIn;
private:
	CCharacter *m_pCharacter;
	CGameContext *m_pGameServer;

	CGameContext *GameServer() const { return m_pGameServer; }
	IServer *Server() const;

	//
	bool m_Spawning;
	int m_ClientID;
	int m_Team;


	// DDRace

public:
	enum
	{
		PAUSED_NONE=0,
		PAUSED_SPEC,
		PAUSED_PAUSED,
		PAUSED_FORCE
	};

	int m_Paused;
	int64 m_NextPauseTick;

	void ProcessPause();
	int m_ForcePauseTime;
	bool IsPlaying();
	int64 m_Last_KickVote;
	int64 m_Last_Team;
	int m_Authed;
	bool m_IsUsingDDRaceClient;
	bool m_ShowOthers;

	int m_ChatScore;

	bool AfkTimer(int new_target_x, int new_target_y); //returns true if kicked
	int64 m_LastPlaytime;
	int64 m_LastEyeEmote;
	int m_LastTarget_x;
	int m_LastTarget_y;
	int m_Sent1stAfkWarning; // afk timer's 1st warning after 50% of sv_max_afk_time
	int m_Sent2ndAfkWarning; // afk timer's 2nd warning after 90% of sv_max_afk_time
	char m_pAfkMsg[160];
	bool m_EyeEmote;
	int m_TimerType;
	int m_DefEmote;
	int m_DefEmoteReset;
#if defined(CONF_SQL)
	int64 m_LastSQLQuery;
#endif
	//XXLmod
	bool m_Invisible;
	bool m_RconFreeze;
	int m_LastLogin;
	// iDDRace64
public:
	// if we are dummy
	bool m_IsDummy;
	//if we are real player (owner)
	int m_DummyID; // our dummy
	bool m_HasDummy;
	bool m_DummyCopiesMove;

	//delay
	int64 m_Last_Dummy; //for control frequency of using /d
	int64 m_Last_DummyChange; //for control frequency of using /dc
};

#endif
