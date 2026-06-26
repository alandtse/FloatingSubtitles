#include "Manager.h"

#include "Compatibility.h"
#include "ImGui/Util.h"
#include "RayCaster.h"
#include "SettingLoader.h"

std::pair<bool, bool> Manager::MCMSettings::LoadMCMSettings(const CSimpleIniA& a_ini)
{
	previous = current;

	current.showGeneralSubtitles = a_ini.GetBoolValue("Settings", "bGeneralSubtitles", current.showGeneralSubtitles);
	current.showDialogueSubtitles = a_ini.GetBoolValue("Settings", "bDialogueSubtitles", current.showDialogueSubtitles);
	current.showDualSubs = a_ini.GetBoolValue("Settings", "bDualSubtitles", current.showDualSubs);

	showSpeakerName = a_ini.GetBoolValue("Settings", "bShowSpeakerName", showSpeakerName);

	subtitleHeadOffset = static_cast<float>(a_ini.GetDoubleValue("Settings", "fHeadOffset", 20.0)) * ModAPIHandler::GetSingleton()->GetResolutionScale();

	doRayCastChecks = a_ini.GetBoolValue("Settings", "bRequireLineOfSight", doRayCastChecks);

	obscuredSubtitleAlpha = static_cast<float>(a_ini.GetDoubleValue("Settings", "fObscuredSubtitleOpacity", obscuredSubtitleAlpha));

	subtitleSpacing = static_cast<float>(a_ini.GetDoubleValue("Settings", "fDualSubtitleSpacing", subtitleSpacing));

	useBTPSWidgetPosition = a_ini.GetBoolValue("Settings", "bUseBTPSWidgetPosition", useBTPSWidgetPosition);
	useTrueHUDWidgetPosition = a_ini.GetBoolValue("Settings", "bUseTrueHUDWidgetPosition", useTrueHUDWidgetPosition);

	subtitleAlphaPrimary = static_cast<float>(a_ini.GetDoubleValue("Settings", "fSubtitleAlphaPrimary", subtitleAlphaPrimary));
	subtitleAlphaSecondary = static_cast<float>(a_ini.GetDoubleValue("Settings", "fSubtitleAlphaSecondary", subtitleAlphaSecondary));

	offscreenSubs = static_cast<OffscreenSubtitle>(a_ini.GetLongValue("Settings", "iOffscreenSubtitles", std::to_underlying(offscreenSubs)));
	maxOffscreenSubs = a_ini.GetLongValue("Settings", "iMaxOffscreenSubtitles", maxOffscreenSubs);
	scrollSubtitles = a_ini.GetBoolValue("Settings", "bScrollSubtitles", scrollSubtitles);
	debugLog = a_ini.GetBoolValue("Settings", "bDebugLog", debugLog);

	return {
		previous.showDualSubs != current.showDualSubs, (!previous.showGeneralSubtitles && current.showGeneralSubtitles || !previous.showDialogueSubtitles && current.showDialogueSubtitles)
	};  // rebuild subs, hide subtitles
}

void Manager::LoadMCMSettings()
{
	SettingLoader::GetSingleton()->Load(FileType::kMCM, [this](auto& ini) {
		bool rebuildSubs = false;
		bool hideSubs = false;

		std::tie(rebuildSubs, hideSubs) = settings.LoadMCMSettings(ini);
		rebuildSubs |= localizedSubs.LoadMCMSettings(ini);

		auto level = settings.debugLog ? spdlog::level::debug : spdlog::level::info;
		spdlog::default_logger()->set_level(level);
		spdlog::default_logger()->flush_on(level);

		// force hide vanilla subtitle
		if (hideSubs) {
			RE::SendHUDMenuMessage(RE::HUD_MESSAGE_TYPE::kHideSubtitle);
		}

		if (rebuildSubs) {
			RebuildProcessedSubtitles();
		}
	});

	localizedSubs.PostMCMSettingsLoad();
}

void Manager::OnDataLoaded()
{
	RE::UI::GetSingleton()->AddEventSink(this);

	localizedSubs.BuildLocalizedSubtitles();

	LoadMCMSettings();

	const auto gameMaxDistance = "fMaxSubtitleDistance:Interface"_ini.value();
	maxDistanceStartSq = gameMaxDistance * gameMaxDistance;
	maxDistanceEndSq = (gameMaxDistance * 1.05f) * (gameMaxDistance * 1.05f);

	logger::info("Max subtitle distance: {:.2f} (start), {:.2f} (end)", gameMaxDistance, gameMaxDistance * 1.05f);

	speakerColorU32 = "iSubtitleSpeakerNameColor:Interface"_ini.value();
	speakerColorFloat4 = ImGui::ColorConvertU32ToFloat4(speakerColorU32);
	speakerColorFloat4.w = 1.0f;

	logger::info("Subtitle speaker color: {},{},{} ({:X})", speakerColorFloat4.x, speakerColorFloat4.y, speakerColorFloat4.z, speakerColorU32);

	//RE::INIPrefSettingCollection::GetSingleton()->GetSetting("bGeneralSubtitles:Interface")->data.b = true;
	//RE::INIPrefSettingCollection::GetSingleton()->GetSetting("bDialogueSubtitles:Interface")->data.b = true;
}

bool Manager::SkipRender() const
{
	if (!visible) {
		return true;
	}

	const bool showGeneral = ShowGeneralSubtitles();
	const bool showDialogue = ShowDialogueSubtitles();

	return !showGeneral && !showDialogue;
}

void Manager::SetVisible(bool a_visible)
{
	visible = a_visible;
}

bool Manager::HandlesGeneralSubtitles() const
{
	return ShowGeneralSubtitles();
}

bool Manager::ShowGeneralSubtitles() const
{
	return "bGeneralSubtitles:Interface"_pref.value() && settings.current.showGeneralSubtitles;
}

bool Manager::HandlesDialogueSubtitles() const
{
	return ShowDialogueSubtitles();
}

bool Manager::ShowDialogueSubtitles() const
{
	return "bDialogueSubtitles:Interface"_pref.value() && settings.current.showDialogueSubtitles && !ModAPIHandler::GetSingleton()->ACCInstalled();
}

DualSubtitle Manager::CreateDualSubtitles(const char* subtitle) const
{
	auto primarySub = localizedSubs.GetPrimarySubtitle(subtitle);
	if (settings.current.showDualSubs) {
		auto secondarySub = localizedSubs.GetSecondarySubtitle(subtitle);
		if (!primarySub.empty() && !secondarySub.empty() && primarySub != secondarySub) {
			return DualSubtitle(primarySub, secondarySub);
		}
	}
	return DualSubtitle(primarySub);
}

void Manager::AddProcessedSubtitle(const char* subtitle)
{
	WriteLocker locker(subtitleLock);
	processedSubtitles.try_emplace(subtitle, CreateDualSubtitles(subtitle));
}

const DualSubtitle& Manager::GetProcessedSubtitle(const RE::BSString& a_subtitle)
{
	{
		ReadLocker readLock(subtitleLock);
		if (auto it = processedSubtitles.find(a_subtitle.c_str()); it != processedSubtitles.end()) {
			return it->second;
		}
	}

	WriteLocker writeLock(subtitleLock);
	auto [it, inserted] = processedSubtitles.try_emplace(a_subtitle.c_str(), CreateDualSubtitles(a_subtitle.c_str()));
	return it->second;
}

void Manager::DrawProcessedSubtitle(const RE::BSString& a_subtitle, const DualSubtitle::ScreenParams& a_params)
{
	{
		ReadLocker writeLock(subtitleLock);
		if (auto it = processedSubtitles.find(a_subtitle.c_str()); it != processedSubtitles.end()) {
			it->second.EnsureWrapped();
			it->second.DrawDualSubtitle(a_params);
			return;
		}
	}

	WriteLocker writeLock(subtitleLock);
	auto [it, inserted] = processedSubtitles.try_emplace(a_subtitle.c_str(), CreateDualSubtitles(a_subtitle.c_str()));
	it->second.EnsureWrapped();
	it->second.DrawDualSubtitle(a_params);
}

void Manager::AddSubtitle(RE::SubtitleManager* a_manager, const char* a_subtitle)
{
	if (!string::is_empty(a_subtitle) && !string::is_only_space(a_subtitle)) {
		AddProcessedSubtitle(a_subtitle);

		RE::BSSpinLockGuard gameLocker(a_manager->lock);
		{
			auto& subtitleArray = reinterpret_cast<RE::BSTArray<RE::SubtitleInfoEx>&>(a_manager->subtitles);
			if (!subtitleArray.empty()) {
				auto& subInfo = subtitleArray.back();
				subInfo.flagsRaw() = 0;  // reset any junk values
				subInfo.alphaModifier() = std::bit_cast<std::uint32_t>(1.0f);
			}
		}
	}
}

void Manager::RebuildProcessedSubtitles()
{
	WriteLocker locker(subtitleLock);
	for (auto& [text, subs] : processedSubtitles) {
		subs = CreateDualSubtitles(text.c_str());
	}
}

void Manager::UpdateSubtitleInfo(RE::SubtitleInfoEx& a_subInfo, bool a_buildOffscreenSubs)
{
	a_subInfo.flagsRaw() = 0;

	const auto& ref = a_subInfo.speaker.get();

	if (a_subInfo.targetDistance == RE::NI_INFINITY) {
		auto refLoc = ref->GetWorldLocation();
		auto playerLoc = RE::PlayerCharacter::GetSingleton()->GetWorldLocation();
		a_subInfo.targetDistance = refLoc.GetSquaredDistance(playerLoc);
	}

	const bool showGeneral = ShowGeneralSubtitles();
	const bool showDialogue = ShowDialogueSubtitles();
	const bool isDialogueSpeaker = RE::MenuTopicManager::GetSingleton()->IsCurrentSpeaker(a_subInfo.speaker);

	if ((isDialogueSpeaker && !showDialogue) ||
		(!isDialogueSpeaker && !showGeneral) ||
		!a_subInfo.forceDisplay && a_subInfo.targetDistance > maxDistanceEndSq) {
		a_subInfo.setFlag(SubtitleFlag::kSkip, true);
		return;
	}

	if (!ref->IsActor() || (ref->IsPlayerRef() && RE::PlayerCamera::GetSingleton()->IsInFirstPerson())) {
		if (a_buildOffscreenSubs) {
			BuildOffscreenSubtitle(ref, a_subInfo.subtitle, isDialogueSpeaker);
		}
		a_subInfo.setFlag(SubtitleFlag::kSkip, true);
		return;
	}

	CalculateVisibility(a_subInfo);

	if (a_subInfo.isFlagSet(SubtitleFlag::kOffscreen)) {
		if (a_buildOffscreenSubs) {
			BuildOffscreenSubtitle(ref, a_subInfo.subtitle, false);
		}
		return;
	}
	if (a_subInfo.isFlagSet(SubtitleFlag::kObscured)) {
		a_subInfo.setFlag(RE::SubtitleInfoEx::Flag::kDraw, settings.obscuredSubtitleAlpha > 0.0f);
	} else {
		a_subInfo.setFlag(SubtitleFlag::kDraw, true);
	}

	CalculateAlphaModifier(a_subInfo);
}

void Manager::CalculateAlphaModifier(RE::SubtitleInfoEx& a_subInfo) const
{
	if (!a_subInfo.isFlagSet(SubtitleFlag::kDraw)) {
		return;
	}

	const auto ref = a_subInfo.speaker.get();
	const auto actor = ref->As<RE::Actor>();

	float alpha = 1.0f;

	if (a_subInfo.isFlagSet(SubtitleFlag::kObscured)) {
		alpha *= settings.obscuredSubtitleAlpha;
	}

	if (a_subInfo.targetDistance > maxDistanceStartSq) {
		const float t = (a_subInfo.targetDistance - maxDistanceStartSq) / (maxDistanceEndSq - maxDistanceStartSq);

		constexpr auto cubicEaseOut = [](float t) -> float {
			return 1.0f - (t * t * t);
		};

		alpha *= 1.0f - cubicEaseOut(t);
	} else if (auto high = actor->GetHighProcess(); high && high->fadeAlpha < 1.0f) {
		alpha *= high->fadeAlpha;
	} else if (actor->IsDead() && actor->GetActorRuntimeData().voiceTimer < 1.0f) {
		alpha *= actor->GetActorRuntimeData().voiceTimer;
	}

	a_subInfo.alphaModifier() = std::bit_cast<std::uint32_t>(alpha);
}

std::string Manager::GetScaleformSubtitle(const RE::BSString& a_subtitle, bool a_dual)
{
	auto subtitle = GetProcessedSubtitle(a_subtitle).GetScaleformCompatibleSubtitle(a_dual);
	return subtitle.empty() ? a_subtitle.c_str() : subtitle;
}

void Manager::BuildOffscreenSubtitle(const RE::TESObjectREFRPtr& a_speaker, const RE::BSString& a_subtitle, bool a_dialogueSubtitle)
{
	if (!a_dialogueSubtitle && offscreenSubCount > settings.maxOffscreenSubs) {
		return;
	}

	bool dualSubs = settings.offscreenSubs == OffscreenSubtitle::kDual;

	auto scaleformSub = GetScaleformSubtitle(a_subtitle, dualSubs);
	if (a_dialogueSubtitle) {
		talkingActivatorSub = scaleformSub;
	} else {
		if (std::string name = ModAPIHandler::GetSingleton()->GetReferenceName(a_speaker); !name.empty()) {
			offscreenSub.append(std::format("<font color='#{:6X}'>{}</font>: {}", speakerColorU32, name, scaleformSub));
		} else {
			offscreenSub.append(scaleformSub);
		}
		offscreenSub.append(dualSubs && scaleformSub.contains("\n") ? "\n\n" : "\n");
	}

	if (!a_dialogueSubtitle) {
		offscreenSubCount++;
	}
}

void Manager::QueueOffscreenSubtitle() const
{
	if (settings.offscreenSubs == OffscreenSubtitle::kDisabled && talkingActivatorSub.empty() && lastTalkingActivatorSub.empty()) {
		return;
	}

	if (lastTalkingActivatorSub != talkingActivatorSub) {
		SKSE::GetTaskInterface()->AddUITask([currentSub = talkingActivatorSub, prevSub = lastTalkingActivatorSub]() {
			if (auto dialogueMenu = RE::UI::GetSingleton()->GetMenu<RE::DialogueMenu>()) {
				if (!prevSub.empty()) {
					RE::FxResponseArgs<0> args{};
					RE::FxDelegate::Invoke(dialogueMenu->uiMovie.get(), "HideDialogueText", args);
				}
				if (!currentSub.empty()) {
					RE::FxResponseArgs<1> args{};
					args.Add(RE::GFxValue(currentSub));
					RE::FxDelegate::Invoke(dialogueMenu->uiMovie.get(), "ShowDialogueText", args);
				}
			}
		});
	} else if (lastOffscreenSub != offscreenSub) {
		SKSE::GetTaskInterface()->AddUITask([currentSub = offscreenSub, prevSub = lastOffscreenSub]() {
			if (auto hudMenu = RE::UI::GetSingleton()->GetMenu<RE::HUDMenu>()) {
				if (!prevSub.empty()) {
					RE::GFxValue subtitleText(prevSub);
					hudMenu->GetRuntimeData().root.Invoke("HideSubtitle", nullptr, &subtitleText, 1);
				}
				if (!currentSub.empty()) {
					RE::GFxValue subtitleText(currentSub);
					hudMenu->GetRuntimeData().root.Invoke("ShowSubtitle", nullptr, &subtitleText, 1);
				}
			}
		});
	}
}

RE::BSEventNotifyControl Manager::ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
	if (a_event && a_event->menuName == RE::DialogueMenu::MENU_NAME && !a_event->opening) {
		talkingActivatorSub.clear();
		lastTalkingActivatorSub.clear();
	}

	return RE::BSEventNotifyControl::kContinue;
}

void Manager::CalculateVisibility(RE::SubtitleInfoEx& a_subInfo)
{
	const auto ref = a_subInfo.speaker.get();
	const auto actor = ref->As<RE::Actor>();

	switch (RayCaster(actor).GetResult(false, GetSingleton()->settings.doRayCastChecks)) {
	case RayCaster::Result::kOffscreen:
		{
			a_subInfo.setFlag(RE::SubtitleInfoEx::Flag::kOffscreen, true);
			a_subInfo.setFlag(RE::SubtitleInfoEx::Flag::kObscured, false);
		}
		break;
	case RayCaster::Result::kObscured:
		{
			a_subInfo.setFlag(RE::SubtitleInfoEx::Flag::kObscured, true);
			a_subInfo.setFlag(RE::SubtitleInfoEx::Flag::kOffscreen, false);
		}
		break;
	case RayCaster::Result::kVisible:
		{
			a_subInfo.setFlag(RE::SubtitleInfoEx::Flag::kOffscreen, false);
			a_subInfo.setFlag(RE::SubtitleInfoEx::Flag::kObscured, false);
		}
		break;
	default:
		std::unreachable();
	}
}

void Manager::UpdateSubtitleInfo(RE::SubtitleManager* a_manager)
{
	if (SkipRender()) {
		return;
	}

	RE::BSSpinLockGuard gameLocker(a_manager->lock);
	{
		lastOffscreenSub = offscreenSub;
		offscreenSub.clear();
		offscreenSubCount = 0;
		lastTalkingActivatorSub = talkingActivatorSub;

		auto& subtitleArray = reinterpret_cast<RE::BSTArray<RE::SubtitleInfoEx>&>(a_manager->subtitles);

		for (auto& subInfo : subtitleArray) {
			if (const auto& ref = subInfo.speaker.get()) {
				UpdateSubtitleInfo(subInfo, true);
			}
		}

		QueueOffscreenSubtitle();
	}
}

RE::NiPoint3 Manager::GetSubtitleAnchorPosImpl(const RE::TESObjectREFRPtr& a_ref, float a_height, bool a_log)
{
	RE::NiPoint3 pos = a_ref->GetPosition();
	if (const auto headNode = RE::GetHeadNode(a_ref)) {
		pos = headNode->world.translate;
		if (a_log) {
			logger::debug("[SubtitlePos] Found head node. world translate: ({:.2f}, {:.2f}, {:.2f})", pos.x, pos.y, pos.z);
		}
	} else {
		pos.z += a_height;
		if (a_log) {
			logger::debug("[SubtitlePos] Head node NOT found, using fallback height offset. Pos with fallback: ({:.2f}, {:.2f}, {:.2f})", pos.x, pos.y, pos.z);
		}
	}
	return pos;
}

RE::NiPoint3 Manager::CalculateSubtitleAnchorPos(const RE::SubtitleInfoEx& a_subInfo, bool a_log) const
{
	const auto ref = a_subInfo.speaker.get();
	const auto height = ref->GetHeight();

	auto pos = GetSubtitleAnchorPosImpl(ref, height, a_log);
	auto offset = settings.subtitleHeadOffset;

	if (a_log) {
		logger::debug("[SubtitlePos] Speaker: {}, Height: {:.2f}, BasePos: ({:.2f}, {:.2f}, {:.2f})",
			ModAPIHandler::GetSingleton()->GetReferenceName(ref), height, ref->GetPosition().x, ref->GetPosition().y, ref->GetPosition().z);
		logger::debug("[SubtitlePos] Head offset config: {:.2f}", settings.subtitleHeadOffset);
	}

	if (auto overridePosZ = ModAPIHandler::GetSingleton()->GetWidgetPosZ(ref, settings.useBTPSWidgetPosition, settings.useTrueHUDWidgetPosition)) {
		pos.z = *overridePosZ;
		offset = settings.subtitleHeadOffset * 0.75f;
		if (a_log) {
			logger::debug("[SubtitlePos] Widget position overridden, Z: {:.2f}, Adjusted offset: {:.2f}", pos.z, offset);
		}
	}

	float finalOffset = offset * (height / 128.0f);
	pos.z += finalOffset;

	if (a_log) {
		logger::debug("[SubtitlePos] Final offset applied: {:.2f}, Calculated anchorPos: ({:.2f}, {:.2f}, {:.2f})", finalOffset, pos.x, pos.y, pos.z);
	}

	return pos;
}

void Manager::Draw()
{
	if (SkipRender()) {
		return;
	}

	const auto subtitleManager = RE::SubtitleManager::GetSingleton();

	RE::BSSpinLockGuard gameLocker(subtitleManager->lock);
	{
		if (stl::IsVR()) {
			ImGui::SetNextWindowPos({ 0.0f, 0.0f });
			ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
		} else {
			ImGui::SetNextWindowPos(ImGui::GetNativeViewportPos());
			ImGui::SetNextWindowSize(ImGui::GetNativeViewportSize());
		}

		ImGui::Begin("##Main", nullptr, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBringToFrontOnFocus);
		{
			ImGui::GetWindowDrawList()->Flags |= ImDrawListFlags_TextNoPixelSnap;

			DualSubtitle::ScreenParams params;
			params.spacing = settings.subtitleSpacing;
			params.speakerColor = speakerColorFloat4;

			bool inFreeCameraMode = RE::PlayerCamera::GetSingleton()->IsInFreeCameraMode();

			auto& subtitleArray = reinterpret_cast<RE::BSTArray<RE::SubtitleInfoEx>&>(subtitleManager->subtitles);

			static std::uint64_t frameCount = 0;
			frameCount++;
			bool logThisFrame = (frameCount % 180 == 0);

			static FlatMap<RE::FormID, float> maxDurations;
			struct CustomTimer {
				std::chrono::steady_clock::time_point startTime;
				float duration;
			};
			static FlatMap<RE::FormID, CustomTimer> customSubtitleTimers;
			for (auto& subInfo : subtitleArray | std::views::reverse) {  // reverse order so closer subtitles get rendered on top
				if (const auto& ref = subInfo.speaker.get()) {
					if (inFreeCameraMode) {
						UpdateSubtitleInfo(subInfo, false);
					}

					if (!subInfo.isFlagSet(SubtitleFlag::kDraw)) {
						continue;
					}

					auto anchorPos = CalculateSubtitleAnchorPos(subInfo, logThisFrame);
					auto zDepth = ImGui::WorldToScreenLoc(anchorPos, params.pos, logThisFrame);
					if (zDepth < 0.0f) {
						continue;
					}

					auto alphaMult = std::bit_cast<float>(subInfo.alphaModifier());
					params.alphaPrimary = settings.subtitleAlphaPrimary * alphaMult;
					params.alphaSecondary = settings.subtitleAlphaSecondary * alphaMult;
					if (settings.showSpeakerName && !RE::IsCrosshairRef(ref)) {
						params.speakerName = ModAPIHandler::GetSingleton()->GetReferenceName(ref);
					} else {
						params.speakerName.clear();
					}

					float elapsedTime = 0.0f;
					float duration = 0.0f;
					auto  extraSay = ref->extraList.GetByType<RE::ExtraSayToTopicInfo>();
					if (extraSay) {
						float remaining = extraSay->subtitleSpeechDelay;
						if (extraSay->sound.IsValid()) {
							duration = static_cast<float>(extraSay->sound.GetDuration()) * 0.001f;
						}

						if (duration <= 0.0f) {
							static FlatMap<RE::FormID, float> silentDurations;
							auto                              formID = ref->GetFormID();
							auto                              it = silentDurations.find(formID);
							if (it == silentDurations.end() || remaining > it->second) {
								silentDurations[formID] = remaining;
								duration = remaining;
							} else {
								duration = it->second;
							}
						}

						elapsedTime = std::clamp(duration - remaining, 0.0f, duration);

						if (logThisFrame && duration > 0.0f) {
							logger::debug("[Manager::Draw] Subtitle '{}' timing (ExtraSayToTopicInfo): remaining={:.2f}s, elapsed={:.2f}s, duration={:.2f}s",
								subInfo.subtitle.c_str(), remaining, elapsedTime, duration);
						}
					} else if (auto actor = ref->As<RE::Actor>()) {
						float remaining = actor->GetActorRuntimeData().timerOnAction;
						if (remaining > 0.0f) {
							auto formID = actor->GetFormID();
							auto it = maxDurations.find(formID);
							if (it == maxDurations.end() || remaining > it->second) {
								maxDurations[formID] = remaining;
								duration = remaining;
							} else {
								duration = it->second;
							}
							elapsedTime = std::clamp(duration - remaining, 0.0f, duration);

							if (logThisFrame && duration > 0.0f) {
								logger::debug("[Manager::Draw] Subtitle '{}' timing (timerOnAction fallback): remaining={:.2f}s, elapsed={:.2f}s, duration={:.2f}s",
									subInfo.subtitle.c_str(), remaining, elapsedTime, duration);
							}
						}
					}

					if (duration <= 0.0f) {
						auto formID = ref->GetFormID();
						auto now = std::chrono::steady_clock::now();
						auto it = customSubtitleTimers.find(formID);
						if (it == customSubtitleTimers.end()) {
							float calcDuration = 2.0f + 0.05f * subInfo.subtitle.length();
							customSubtitleTimers[formID] = { now, calcDuration };
							duration = calcDuration;
							elapsedTime = 0.0f;
						} else {
							duration = it->second.duration;
							elapsedTime = std::chrono::duration<float>(now - it->second.startTime).count();
							elapsedTime = std::clamp(elapsedTime, 0.0f, duration);
						}

						if (logThisFrame && duration > 0.0f) {
							logger::debug("[Manager::Draw] Subtitle '{}' timing (Custom tracking fallback): elapsed={:.2f}s, duration={:.2f}s",
								subInfo.subtitle.c_str(), elapsedTime, duration);
						}
					}

					params.elapsedTime = elapsedTime;
					params.duration = duration;

					// Distance-based font scaling
					float distance = std::sqrt(subInfo.targetDistance);
					float fontScale = 1.0f;
					float maxDist = std::sqrt(maxDistanceStartSq);
					if (maxDist > 200.0f) {
						if (distance > 200.0f) {
							fontScale = 1.0f - ((distance - 200.0f) / (maxDist - 200.0f)) * 0.5f;
							fontScale = std::clamp(fontScale, 0.5f, 1.0f);
						}
					}

					// Check if there is a closer speaker in between (similar angular direction)
					bool closerSpeakerInBetween = false;
					auto playerLoc = RE::PlayerCharacter::GetSingleton()->GetPosition();
					RE::NiPoint3 dirDistant = ref->GetPosition() - playerLoc;
					dirDistant.Unitize();

					for (const auto& otherSubInfo : subtitleArray) {
						if (&otherSubInfo != &subInfo && otherSubInfo.isFlagSet(SubtitleFlag::kDraw)) {
							if (otherSubInfo.targetDistance < subInfo.targetDistance) {
								if (const auto otherRef = otherSubInfo.speaker.get()) {
									RE::NiPoint3 dirCloser = otherRef->GetPosition() - playerLoc;
									dirCloser.Unitize();
									float dot = dirDistant.x * dirCloser.x + dirDistant.y * dirCloser.y + dirDistant.z * dirCloser.z;
									// cos(25 degrees) ≈ 0.906
									if (dot > 0.906f) {
										closerSpeakerInBetween = true;
										break;
									}
								}
							}
						}
					}

					if (closerSpeakerInBetween) {
						fontScale *= 0.75f;
						fontScale = std::clamp(fontScale, 0.40f, 1.0f);
					}

					params.fontScale = fontScale;

					if (logThisFrame) {
						logger::debug("[Manager::Draw] Speaker '{}' distance={:.1f}, fontScale={:.2f}, inBetween={}",
							ModAPIHandler::GetSingleton()->GetReferenceName(ref), distance, fontScale, closerSpeakerInBetween);
					}

					DrawProcessedSubtitle(subInfo.subtitle, params);
				}
			}

			// Clean up maxDurations for actors that are no longer speaking
			std::vector<RE::FormID> activeFormIDs;
			for (auto& subInfo : subtitleArray) {
				if (const auto& ref = subInfo.speaker.get()) {
					activeFormIDs.push_back(ref->GetFormID());
				}
			}
			for (auto it = maxDurations.begin(); it != maxDurations.end();) {
				if (std::find(activeFormIDs.begin(), activeFormIDs.end(), it->first) == activeFormIDs.end()) {
					it = maxDurations.erase(it);
				} else {
					++it;
				}
			}
			for (auto it = customSubtitleTimers.begin(); it != customSubtitleTimers.end();) {
				if (std::find(activeFormIDs.begin(), activeFormIDs.end(), it->first) == activeFormIDs.end()) {
					it = customSubtitleTimers.erase(it);
				} else {
					++it;
				}
			}
		}
		ImGui::End();
	}
}
