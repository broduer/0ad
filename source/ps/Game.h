/* Copyright (C) 2026 Wildfire Games.
 * This file is part of 0 A.D.
 *
 * 0 A.D. is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * 0 A.D. is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with 0 A.D.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef INCLUDED_GAME
#define INCLUDED_GAME

#include "graphics/FramePacket.h"
#include "lib/code_annotation.h"
#include "lib/os_path.h"
#include "ps/CStr.h"
#include "ps/Errors.h"
#include "simulation2/helpers/Player.h"
#include "simulation2/system/DebugOptions.h"

#include <algorithm>
#include <cmath>
#include <iosfwd>
#include <js/TypeDecls.h>
#include <string>
#include <vector>

class CGameView;
class CSimulation2;
class CTurnManager;
class CWorld;
class IReplayLogger;

/**
 * The container that holds the rules, resources and attributes of the game.
 * The CGame object is responsible for creating a game that is defined by
 * a set of attributes provided. The CGame object is also responsible for
 * maintaining the relations between CPlayer and CWorld, CSimulation and CWorld.
 **/
class CGame
{
	NONCOPYABLE(CGame);

	/**
	 * pointer to the CWorld object representing the game world.
	 **/
	CWorld *m_World;

	/**
	 * pointer to the CSimulation2 object operating on the game world.
	 **/
	CSimulation2 *m_Simulation2;

	/**
	 * pointer to the CGameView object representing the view into the game world.
	 **/
	CGameView *m_GameView;

	/**
	 * the game has been initialized and ready for use if true.
	 **/
	bool m_GameStarted;

	/**
	 * Timescale multiplier for simulation rate.
	 **/
	float m_SimRate;

	/**
	 * Index assigned to the current player.
	 * 1-8 to control players, 0 for gaia, -1 for observer.
	 */
	player_id_t m_PlayerID;

	/**
	 * Differs from m_PlayerID if a defeated player or observer views another player.
	 */
	player_id_t m_ViewedPlayerID;

	CTurnManager* m_TurnManager;

	// Logic thread: frame state sent with each frame step's packet.
	float m_InterpolationSimFrameLength = 0.0f;
	float m_InterpolationOffset = 0.0f;
	float m_InterpolationRealFrameLength = 0.0f;
	// Simulation time the renderer's time manager has yet to advance by.
	float m_TimeManagerDelta = 0.0f;
	// Real time UpdateAhead advanced by that no Update has covered yet.
	double m_RealTimeAhead = 0.0;
	// Player colors are script state, so they change only with a turn or the
	// viewed player.
	bool m_PlayerColorsDirty = true;

	// Render thread: timing of the frame steps applied since the last
	// RenderInterpolate.
	RenderWorld::CFramePacket::STiming m_RenderTiming;

	/**
	 * Step the simulation's frame and send its packet. Logic thread only.
	 */
	void PublishFrame(float deltaSimTime, float offset, float deltaRealTime);

	/**
	 * Hand the simulation's records since the last call to the render world's
	 * queue, with the frame's timing and view if given. Logic thread.
	 */
	void PushRenderPacket(const RenderWorld::CFramePacket::STiming* timing = nullptr);

	/**
	 * Apply every queued packet to the render world. Render thread, or the
	 * logic thread while the render thread is paused.
	 */
	void ApplyRenderPackets();

	// Render thread: packets being applied, kept to reuse their storage.
	std::vector<RenderWorld::CFramePacket> m_RenderPackets;

public:
	CGame(bool replayLog, const SimulationDebugOptions debugOptions = {});
	~CGame();

	/**
	 * the game is paused and no updates will be performed if true.
	 **/
	bool m_Paused;

	void StartGame(JS::MutableHandleValue attribs, const std::string& savedState);
	PSRETURN ReallyStartGame();

	bool StartVisualReplay(const OsPath& replayPath);

	/**
	 * Periodic heartbeat that controls the process. performs all per-frame updates.
	 * Simulation update is called and game status update is called.
	 *
	 * @param deltaRealTime Elapsed real time since last beat/frame, in seconds.
	 * @param doInterpolate Perform graphics interpolation if true.
	 * @return bool false if it can't keep up with the desired simulation rate
	 *	indicating that you might want to render less frequently.
	 */
	void Update(const double deltaRealTime, bool doInterpolate = true);

	/**
	 * Advance the simulation now by the real time the next Update is expected
	 * to cover, so a turn due then doesn't delay that step's input. Commands
	 * are scheduled turns ahead, so a turn needs none from its own step. The
	 * next Update advances only by any difference.
	 */
	void UpdateAhead(const double deltaRealTime);

	void Interpolate(float simFrameLength, float realFrameLength);

	/**
	 * Hand the simulation's records to the render world and apply them at
	 * once. Call only while the render thread is paused, as when loading.
	 */
	void FlushRenderPackets();

	/**
	 * Give the simulation what the render world last reported, for the coming
	 * logic step. Logic thread.
	 */
	void RefreshRenderFeedback();

	/**
	 * Step the simulation's frame and send the render thread its packet:
	 * component records, frame timing and the view. Logic thread only.
	 */
	void FrameStep(float deltaRealTime);

	/**
	 * Apply the queued packets and interpolate the render world. Render
	 * thread only.
	 */
	void RenderInterpolate();

	/**
	 * Apply the queued packets and interpolate with explicit frame lengths,
	 * for a caller that drives the simulation itself (Atlas).
	 */
	void RenderInterpolate(float simFrameLength, float realFrameLength);

	int GetPlayerID();
	void SetPlayerID(player_id_t playerID);

	int GetViewedPlayerID();
	void SetViewedPlayerID(player_id_t playerID);

	bool CheatsEnabled() const;

	/**
	 * Check if the game is finished by testing if there's a winner.
	 * It is used to end a non visual autostarted game.
	 *
	 * @return true if there's a winner, false otherwise.
	 */
	bool IsGameFinished() const;

	/**
	 * Check if the given player has been defeated or won the game.
	 */
	bool PlayerFinished(player_id_t playerID) const;

	/**
	 * Return all players' colors. Reading them from scripts is slow, so the
	 * frame step sends them to the render thread only after they may have changed.
	 */
	std::vector<CColor> GetPlayerColors();

	/**
	 * Resend the player colors with the next frame step.
	 */
	void RefreshPlayerColors() { m_PlayerColorsDirty = true; }

	const CColor& GetPlayerColor(player_id_t player) const;

	/**
	 * Get m_GameStarted.
	 *
	 * @return bool the value of m_GameStarted.
	 **/
	inline bool IsGameStarted() const
	{
		return m_GameStarted;
	}

	/**
	 * Get m_IsVisualReplay.
	 *
	 * @return bool the value of m_IsVisualReplay.
	 **/
	inline bool IsVisualReplay() const
	{	return m_IsVisualReplay; }

	/**
	 * Get the pointer to the game world object.
	 *
	 * @return CWorld * the value of m_World.
	 **/
	inline CWorld *GetWorld()
	{	return m_World; }

	/**
	 * Get the pointer to the game view object.
	 *
	 * @return CGameView * the value of m_GameView.
	 **/
	inline CGameView *GetView()
	{	return m_GameView; }

	/**
	 * Get the pointer to the simulation2 object.
	 *
	 * @return CSimulation2 * the value of m_Simulation2.
	 **/
	inline CSimulation2 *GetSimulation2()
	{	return m_Simulation2; }

	/**
	 * Set the simulation scale multiplier.
	 *
	 * @param simRate Float value to set m_SimRate to.
	 *						Because m_SimRate is also used to
	 *						scale TimeSinceLastFrame it must be
	 *						clamped to 0.0f.
	 **/
	inline void SetSimRate(float simRate)
	{	if (std::isfinite(simRate)) m_SimRate = std::max(simRate, 0.0f); }

	inline float GetSimRate() const
	{	return m_SimRate; }

	inline OsPath GetReplayPath() const
	{	return m_ReplayPath; }

	/**
	 * Replace the current turn manager.
	 * This class will take ownership of the pointer.
	 */
	void SetTurnManager(CTurnManager* turnManager);

	CTurnManager* GetTurnManager() const
	{	return m_TurnManager; }

	IReplayLogger& GetReplayLogger() const
	{	return *m_ReplayLogger; }

private:
	static const CStr EventNameSimulationUpdate;

	void RegisterInit(const JS::HandleValue attribs, const std::string& savedState);
	// Process the turns due within @p deltaSimTime.
	void Advance(const double deltaSimTime);
	IReplayLogger* m_ReplayLogger;

	std::vector<CColor> m_PlayerColors;

	int LoadInitialState(const std::string& savedState);

	bool m_CheatsEnabled;

	int LoadVisualReplayData();
	OsPath m_ReplayPath;
	bool m_IsVisualReplay;
	std::istream* m_ReplayStream;
	std::uint32_t m_FinalReplayTurn;
};

extern CGame *g_Game;

/**
 * Create or delete g_Game with the render thread paused, as the game's view
 * owns render resources. Logic thread.
 */
void CreateGame(bool replayLog, const SimulationDebugOptions debugOptions = {});
void DeleteGame();

#endif
