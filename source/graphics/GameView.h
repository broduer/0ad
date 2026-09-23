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

#ifndef INCLUDED_GAMEVIEW
#define INCLUDED_GAMEVIEW

#include "lib/code_annotation.h"
#include "renderer/Scene.h"
#include "simulation2/system/Entity.h"

#include <memory>

class CCamera;
class CCinemaManager;
class CGame;
class CGameViewImpl;
class CObjectManager;
class CVector3D;
namespace RenderWorld { class CPacketQueue; class CRenderWorld; struct SLosGrid; }
namespace Renderer::Backend { class IDevice; }
namespace Renderer::Backend { class IDeviceCommandContext; }
struct SViewFrame;
struct SViewPort;
union SDL_Event;

class CGameView : private Scene
{
	NONCOPYABLE(CGameView);
public:
	CGameView(Renderer::Backend::IDevice* device, CGame *pGame);
	~CGameView() override;

	void SetViewport(const SViewPort& vp);

	void RegisterInit();

	/**
	 * Updates all the view information (i.e. rotate camera, scroll, whatever). This will *not* change any
	 * World information - only the *presentation*.
	 *
	 * @param deltaRealTime Elapsed real time since the last frame.
	 */
	void Update(const float deltaRealTime);

	/**
	 * Prepare the bitmaps and minimap entities and hand them over with the
	 * cameras for the frame packet. Logic thread only.
	 */
	void PrepareFrameStep(float realTimeDelta);
	std::shared_ptr<SViewFrame> PublishFrameStep();

	/**
	 * Return the displayed player's LOS grid if it changed since the last
	 * call. Logic thread only.
	 */
	std::shared_ptr<const RenderWorld::SLosGrid> TakeLosGrid();

	/**
	 * Take a frame packet's cameras and texture data. Render thread, or the
	 * logic thread while the render thread is paused.
	 */
	void TakePublished(SViewFrame& frame);

	/**
	 * Return whether the render thread culls, as of the last frame packet.
	 */
	bool IsRenderCulling() const;

	void EnumerateSceneObjects();

	CVector3D GetCameraPivot() const;
	CVector3D GetCameraPosition() const;
	CVector3D GetCameraRotation() const;
	float GetCameraZoom() const;

	void SetCamera(const CVector3D& pos, float rotX, float rotY, float zoom);
	void MoveCameraTarget(const CVector3D& target);
	void LoadCameraConfig();
	void ResetCameraTarget(const CVector3D& target);
	void FollowEntity(entity_id_t entity, bool firstPerson);
	entity_id_t GetFollowedEntity();

	#define DECLARE_BOOLEAN_SETTING(NAME) \
	bool Get##NAME##Enabled() const; \
	void Set##NAME##Enabled(bool Enabled);

	DECLARE_BOOLEAN_SETTING(Culling);
	DECLARE_BOOLEAN_SETTING(LockCullCamera);
	DECLARE_BOOLEAN_SETTING(ConstrainCamera);

	#undef DECLARE_BOOLEAN_SETTING

	const CCamera& GetCamera() const;
	void SetCamera(const CCamera& camera);

	const CCamera& GetCullCamera() const;

	CCinemaManager* GetCinema();
	CObjectManager& GetObjectManager();

	/**
	 * Render thread only, or the logic thread while the render thread is paused.
	 */
	RenderWorld::CRenderWorld& GetRenderWorld();
	RenderWorld::CPacketQueue& GetPacketQueue();

	// Implementations of Scene
	void EnumerateObjects(const CFrustum& frustum, SceneCollector* c) override;
	CLOSTexture& GetLOSTexture() override;
	CTerritoryTexture& GetTerritoryTexture() override;
	CMiniMapTexture& GetMiniMapTexture() override;

private:
	// Unloads all graphics resources loaded by RegisterInit.
	void UnloadResources();
	void UpdateCamera(const float deltaRealTime);
	void UpdateCullCamera();

	CGameViewImpl* m;
};

#endif // INCLUDED_GAMEVIEW
