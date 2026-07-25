#include "RayCaster.h"

#include "ImGui/Renderer.h"
#include "ImGui/Util.h"

RayCollector::RayCollector(RE::Actor* a_actor, RE::COL_LAYER a_layer) :
	actor(a_actor)
{
	RE::CFilter actorFilter;
	a_actor->GetCollisionFilterInfo(actorFilter);
	RE::bhkCollisionFilter::GetSingleton()->layerBitfields[std::to_underlying(a_layer)] = (1LL << (actorFilter.filter & RE::CFilter::Flags::kLayerMask)) | 0x22757;
}

void RayCollector::AddRayHit(const RE::hkpCdBody& a_body, const RE::hkpShapeRayCastCollectorOutput& a_hitInfo)
{
	const auto* body = std::addressof(a_body);
	for (const auto* parent = body->parent; parent; parent = parent->parent) {
		body = parent;
	}

	if (!body) {
		return;
	}

	const auto rootCollidable = static_cast<const RE::hkpCollidable*>(body);
	auto       rootColFilter = rootCollidable->broadPhaseHandle.collisionFilterInfo;

	switch (rootColFilter.GetCollisionLayer()) {
	case RE::COL_LAYER::kStatic:
	case RE::COL_LAYER::kTerrain:
	case RE::COL_LAYER::kGround:
		hkpClosestRayHitCollector::AddRayHit(a_body, a_hitInfo);
		break;
	case RE::COL_LAYER::kBiped:
	case RE::COL_LAYER::kBipedNoCC:
	case RE::COL_LAYER::kDeadBip:
	case RE::COL_LAYER::kCharController:
		{
			if (const auto owner = RE::TESHavokUtilities::FindCollidableRef(*rootCollidable); owner && owner == actor) {
				hkpClosestRayHitCollector::AddRayHit(a_body, a_hitInfo);
			}
		}
		break;
	default:
		break;
	}
}

void RayCaster::StartPoint::Init()
{
	auto player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		return;
	}

	debug = player->GetPosition();
	debug.z += player->GetInfoRuntimeData().eyeHeight;

	camera = RE::PlayerCamera::GetActiveCameraPosition();
	if (camera == RE::NiPoint3::Zero()) {
		camera = debug;
	}
}

RayCaster::RayCaster(RE::Actor* a_target) :
	actor(a_target)
{
	startPoint.Init();
}

RayCaster::Result RayCaster::GetResult(bool a_debugRay, bool a_doRayCast)
{
	auto* root = actor->Get3D();
	if (!root) {
		return Result::kOffscreen;
	}

	if (REL::Module::IsVR()) {
		// Project with the camera directly instead of PointInFrustum: in VR it reads
		// NiCamera::viewFrustumArray, which is left-eye only (per GetNearPlane's own
		// "return left in VR"), not a combined stereo frustum -- it can miss actors
		// visible only in the right eye. Do NOT route through ImGui::WorldToScreenLoc
		// here: GetResult runs on the game update thread (PlayerCharacter::Update)
		// where our ImGui context isn't current, so GetIO() would dereference a null
		// context and crash.
		auto* camera = RE::Main::WorldRootCamera();
		if (!camera) {
			return Result::kOffscreen;
		}
		const auto&       worldToCam = camera->GetVRRuntimeData().worldToCam;
		const auto&       bound = root->worldBound;
		RE::NiRect<float> port(0.0f, 1.0f, 1.0f, 0.0f);
		float             x = 0.0f, y = 0.0f, z = -1.0f;
		if (!RE::NiCamera::WorldPtToScreenPt3(worldToCam, port, bound.center, x, y, z, 1e-5f)) {
			return Result::kOffscreen;
		}
		// Expand the on-screen test by the actor's bound radius in screen space, so an actor whose
		// center is just past the edge but whose body is still visible isn't wrongly flagged
		// off-screen (parity with the flat PointInFrustum(center, radius) test).
		float margin = 0.0f;
		for (const auto& off : { RE::NiPoint3(bound.radius, 0.0f, 0.0f), RE::NiPoint3(0.0f, 0.0f, bound.radius) }) {
			float ox = 0.0f, oy = 0.0f, oz = 0.0f;
			if (RE::NiCamera::WorldPtToScreenPt3(worldToCam, port, bound.center + off, ox, oy, oz, 1e-5f)) {
				margin = std::max({ margin, std::fabs(ox - x), std::fabs(oy - y) });
			}
		}
		// Subtitles on the head-locked HUD plane fill only its coverage fraction of the view, so
		// warp the test to match. World-quad billboards are visible across the whole view — test
		// the raw [0,1] frustum instead.
		if (!ImGui::Renderer::WorldQuadActive()) {
			if (const float coverage = ImGui::Renderer::GetHUDCoverage(); coverage > 0.0f) {
				x = 0.5f + (x - 0.5f) / coverage;
				y = 0.5f + (y - 0.5f) / coverage;
				margin /= coverage;
			}
		}
		if (z < 0.0f || x < -margin || x > 1.0f + margin || y < -margin || y > 1.0f + margin) {
			return Result::kOffscreen;
		}
	} else if (auto* camera = RE::Main::WorldRootCamera()) {
		if (!camera->PointInFrustum(root->worldBound.center, root->worldBound.radius)) {
			return Result::kOffscreen;
		}
	}

	auto cell = actor->parentCell;
	auto bhkWorld = cell ? cell->GetbhkWorld() : nullptr;

	if (!bhkWorld) {
		return Result::kOffscreen;  // can't raycast so might as well return true
	}

	if (!a_doRayCast) {
		return Result::kVisible;
	}

	targetPoints[0] = actor->CalculateLOSLocation(RE::ACTOR_LOS_LOCATION::kEye);
	targetPoints[1] = actor->CalculateLOSLocation(RE::ACTOR_LOS_LOCATION::kHead);
	targetPoints[2] = actor->CalculateLOSLocation(RE::ACTOR_LOS_LOCATION::kTorso);
	targetPoints[3] = actor->CalculateLOSLocation(RE::ACTOR_LOS_LOCATION::kFeet);

	RE::bhkPickData pickData{};

	const auto havokWorldScale = RE::bhkWorld::GetWorldScale();
	pickData.rayInput.from = startPoint.camera * havokWorldScale;
	pickData.rayInput.enableShapeCollectionFilter = false;
	pickData.rayInput.filterInfo.SetCollisionLayer(RE::COL_LAYER::kLOS);

	RayCollector collector(actor, RE::COL_LAYER::kLOS);
	pickData.closestRayHitCollector = &collector;

	bool result = false;

	for (std::uint32_t i = 0; i < targetPoints.size(); ++i) {
		if (result) {
			break;
		}

		collector.Reset();
		pickData.rayInput.to = targetPoints[i] * havokWorldScale;

		if (bhkWorld->PickObject(pickData)) {
			if (auto* collidable = pickData.rayOutput.rootCollidable) {
				result = RE::TESHavokUtilities::FindCollidableRef(*collidable) == actor;
			}
		}

		if (a_debugRay) {
			DebugRay(pickData, targetPoints[i], debugColors[i]);
		}
	}

	return result ? Result::kVisible : Result::kObscured;
}

void RayCaster::DebugRay(const RE::bhkPickData& a_pickData, const RE::NiPoint3& a_targetPos, ImU32 color) const
{
	const auto hitPos = (a_targetPos - startPoint.debug) * a_pickData.rayOutput.hitFraction + startPoint.debug;

	ImGui::DrawLine(startPoint.debug, hitPos, a_pickData.rayOutput.HasHit() ? color : IM_COL32_BLACK);

	if (auto* collidable = a_pickData.rayOutput.rootCollidable) {
		auto node = RE::TESHavokUtilities::FindCollidableObject(*collidable);
		auto userData = node ? node->GetUserData() : nullptr;

		std::string nodeName = node ? node->name.c_str() : "";
		if (node && nodeName.empty()) {
			nodeName = node->GetRTTI()->GetName();
		}

		auto text = std::format("{} : {} [{}]",
			userData ? userData->GetDisplayFullName() : "",
			nodeName,
			collidable->broadPhaseHandle.collisionFilterInfo.GetCollisionLayer());

		ImGui::DrawTextAtPoint(hitPos, text.c_str(), color);
	}
}
