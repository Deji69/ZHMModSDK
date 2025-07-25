#include "Editor.h"

#include <numbers>

#include "Hooks.h"
#include "Logging.h"

#include <Glacier/ZScene.h>

#include "IconsMaterialDesign.h"
#include "Glacier/ZGeomEntity.h"
#include "Glacier/ZModule.h"
#include "Glacier/ZSpatialEntity.h"
#include "Glacier/ZCameraEntity.h"
#include "Glacier/ZRender.h"
#include "Glacier/EntityFactory.h"
#include <ranges>

#include "backends/imgui_impl_dx12.h"
#include "Glacier/SGameUpdateEvent.h"
#include "Glacier/ZCollision.h"
#include "Glacier/ZActor.h"
#include "Glacier/ZGameLoopManager.h"
#include "Glacier/ZKnowledge.h"

Editor::Editor() {
    // Disable ZTemplateEntityBlueprintFactory freeing its associated data.
    uint8_t s_Nop[0x45] = {};
    memset(s_Nop, 0x90, sizeof(s_Nop));

    if (!SDK()->PatchCode(
        "\x48\x85\xDB\x74\x00\xE8\x00\x00\x00\x00\x48\x8B\x08\x48\x85\xC9\x75\x00\xE8\x00\x00\x00\x00\x48\x8B\x48\x10\x48\x8B\x01\xEB\x00\x48\x8B\x01\x48\x8B\xD3\xFF\x50\x40\x48\x8B\xC8\x48\x85\xC0\x74\x00\x48\x8B\x00\x48\x8B\xD3\xFF\x50\x48\x48\xC7\x86\xA0\x01\x00\x00",
        "xxxx?x????xxxxxxx?x????xxxxxxxx?xxxxxxxxxxxxxxxx?xxxxxxxxxxxxxxxx",
        s_Nop,
        sizeof(s_Nop),
        0
    )) {
        Logger::Error("Could not patch ZTemplateEntityBlueprintFactory data freeing.");
    }

    if (!SDK()->PatchCode(
        "\x48\x85\xFF\x74\x00\xE8\x00\x00\x00\x00\x48\x8B\x08\x48\x85\xC9\x75\x00\xE8\x00\x00\x00\x00\x48\x8B\x48\x10\x48\x8B\x01\xEB\x00\x48\x8B\x01\x48\x8B\xD7\xFF\x50\x40\x48\x8B\xC8\x48\x85\xC0\x74\x00\x48\x8B\x00\x48\x8B\xD7\xFF\x50\x48\x48\xC7\x83\xA0\x01\x00\x00",
        "xxxx?x????xxxxxxx?x????xxxxxxxx?xxxxxxxxxxxxxxxx?xxxxxxxxxxxxxxxx",
        s_Nop,
        sizeof(s_Nop),
        0
    )) {
        Logger::Error("Could not patch ZTemplateEntityBlueprintFactory brick data freeing.");
    }

    // Initialize Winsock and create the Qne socket and relevant things.
    WSADATA s_Ws;
    if (WSAStartup(MAKEWORD(2, 2), &s_Ws) != 0) {
        Logger::Error("WSAStartup failed: %d", WSAGetLastError());
        return;
    }

    if ((m_QneSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == INVALID_SOCKET) {
        Logger::Error("Could not create socket: %d", WSAGetLastError());
        return;
    }

    // Make socket non-blocking.
    u_long s_NonBlocking = 1;
    if (ioctlsocket(m_QneSocket, FIONBIO, &s_NonBlocking) != 0) {
        Logger::Error("Could not make socket non-blocking: %d", WSAGetLastError());
        return;
    }

    m_QneAddress.sin_family = AF_INET;
    m_QneAddress.sin_port = htons(49494);
    m_QneAddress.sin_addr.S_un.S_addr = inet_addr("127.0.0.1");

    m_raycastLogging = false;
}

Editor::~Editor() {
    const ZMemberDelegate<Editor, void(const SGameUpdateEvent&)> s_Delegate(this, &Editor::OnFrameUpdate);
    Globals::GameLoopManager->UnregisterFrameUpdate(s_Delegate, 1, EUpdateMode::eUpdatePlayMode);

    if (m_SelectionForFreeCameraEditorStyleEntity) {
        Globals::Selections->clear();
        free(m_SelectionForFreeCameraEditorStyleEntity);
        m_SelectionForFreeCameraEditorStyleEntity = nullptr;
    }
}

void Editor::Init() {
    Hooks::ZEntitySceneContext_LoadScene->AddDetour(this, &Editor::OnLoadScene);
    Hooks::ZEntitySceneContext_ClearScene->AddDetour(this, &Editor::OnClearScene);
    Hooks::ZTemplateEntityBlueprintFactory_ZTemplateEntityBlueprintFactory->AddDetour(
        this, &Editor::ZTemplateEntityBlueprintFactory_ctor
    );
    Hooks::SignalInputPin->AddDetour(this, &Editor::OnInputPin);
    Hooks::SignalOutputPin->AddDetour(this, &Editor::OnOutputPin);

    m_UseSnap = GetSettingBool("general", "snap", true);
    m_SnapValue = GetSettingDouble("general", "snap_value", 1.0);
    m_UseAngleSnap = GetSettingBool("general", "angle_snap", true);
    m_AngleSnapValue = GetSettingDouble("general", "angle_snap_value", 90.0);
    m_UseScaleSnap = GetSettingBool("general", "scale_snap", true);
    m_ScaleSnapValue = GetSettingDouble("general", "scale_snap_value", 1.0);
    m_UseQneTransforms = GetSettingBool("general", "qne_transforms", false);
}

void Editor::OnDrawMenu() {
    if (ImGui::Button(ICON_MD_VIDEO_SETTINGS "  EDITOR")) {
        m_ShowUI = !m_ShowUI;
    }

    if (m_ShowUI) {
        bool s_ServerEnabled = m_Server.GetEnabled();
        if (ImGui::Checkbox("EDITOR SERVER", &s_ServerEnabled)) {
            ToggleEditorServerEnabled();
        }
    }

    /*if (ImGui::Button(ICON_MD_VIDEO_SETTINGS "  EDITOR"))
    {
        const auto s_Scene = Globals::Hitman5Module->m_pEntitySceneContext->m_pScene;

        if (s_Scene)
        {
            for (auto& s_Brick : Globals::Hitman5Module->m_pEntitySceneContext->m_aLoadedBricks)
            {
                if (s_Brick.runtimeResourceID != ResId<"[assembly:/_sdk/editor/editor_data.brick].pc_entitytype">)
                    continue;

                Logger::Debug("Found editor_data brick.");

                const auto s_BpFactory = reinterpret_cast<ZTemplateEntityBlueprintFactory*>(s_Brick.entityRef.GetBlueprintFactory());

                const auto s_Index = s_BpFactory->GetSubEntityIndex(0xfeedbf5a41eb9c48);

                if (s_Index != -1)
                {
                    Logger::Debug("Found RT at index {}.", s_Index);
                    m_CameraRT = s_BpFactory->GetSubEntity(s_Brick.entityRef.m_pEntity, s_Index);

                    const auto s_CameraRTEntity = m_CameraRT.QueryInterface<ZRenderDestinationTextureEntity>();
                    const auto s_RT = reinterpret_cast<ZRenderDestination*>(s_CameraRTEntity->GetRenderDestination());

                    Logger::Debug("RTEntity = {} RT = {}", fmt::ptr(s_CameraRTEntity), fmt::ptr(s_RT));

                    const auto s_Camera = Functions::GetCurrentCamera->Call();

                    ZEntityRef s_CameraRef;
                    s_Camera->GetID(&s_CameraRef);

                    s_CameraRTEntity->AddClient(s_CameraRef);

                    for (auto& s_Client : *s_CameraRTEntity->GetClients())
                    {
                        Logger::Debug("RT client = {} {:x} {}", fmt::ptr(s_Client.GetEntity()), s_Client->GetType()->m_nEntityId, (*s_Client->GetType()->m_pInterfaces)[0].m_pTypeId->typeInfo()->m_pTypeName);
                    }
                }

                const auto s_CameraIndex = s_BpFactory->GetSubEntityIndex(0xfeedb6fc4f5626ea);

                if (s_CameraIndex != -1)
                {
                    Logger::Debug("Found Cam at index {}.", s_CameraIndex);
                    m_Camera = s_BpFactory->GetSubEntity(s_Brick.entityRef.m_pEntity, s_CameraIndex);

                    Logger::Debug("CamEntity = {}", fmt::ptr(m_Camera.GetEntity()));
                }

                break;
            }
        }
    }*/
}


void Editor::ToggleEditorServerEnabled() {
    m_Server.SetEnabled(!m_Server.GetEnabled());
}

void Editor::CopyToClipboard(const std::string& p_String) const {
    if (!OpenClipboard(nullptr))
        return;

    EmptyClipboard();

    const auto s_GlobalData = GlobalAlloc(GMEM_MOVEABLE, p_String.size() + 1);

    if (!s_GlobalData) {
        CloseClipboard();
        return;
    }

    const auto s_GlobalDataPtr = GlobalLock(s_GlobalData);

    if (!s_GlobalDataPtr) {
        CloseClipboard();
        GlobalFree(s_GlobalData);
        return;
    }

    memset(s_GlobalDataPtr, 0, p_String.size() + 1);
    memcpy(s_GlobalDataPtr, p_String.c_str(), p_String.size());

    GlobalUnlock(s_GlobalData);

    SetClipboardData(CF_TEXT, s_GlobalData);
    CloseClipboard();
}

void Editor::OnDraw3D(IRenderer* p_Renderer) {
    DrawEntityAABB(p_Renderer);

    /*const auto s_Color = SVector4(0.88, 0.88, 0.08, 0.4);
    const auto s_LineColor = SVector4(0.94, 0.12, 0.05, 1.0);

    p_Renderer->DrawQuad3D(
        { -26.179094, -25.697458, 0.5 },
        s_Color,
        { -25.915297, -27.365824, 0.5 },
        s_Color,
        { -27.750357, -27.950037, 0.5 },
        s_Color,
        { -27.304773, -25.154234, 0.5 },
        s_Color
    );*/

    //p_Renderer->DrawLine3D({ -35.352013, -23.58427, 0.4925564 }, { -27.71298, -24.866821, 0.4925564 }, s_LineColor, s_LineColor);
    //p_Renderer->DrawLine3D({ -27.71298, -24.866821, 0.4925564 }, { -26.691515, -38.064953, 0.4925564 }, s_LineColor, s_LineColor);
    //p_Renderer->DrawLine3D({ -26.691515, -38.064953, 0.4925564 }, { -41.43283, -33.25945, 0.49255627 }, s_LineColor, s_LineColor);
    //p_Renderer->DrawLine3D({ -41.43283, -33.25945, 0.49255627 }, { -35.352013, -23.58427, 0.4925564 }, s_LineColor, s_LineColor);
}

void Editor::OnEngineInitialized() {
    const ZMemberDelegate<Editor, void(const SGameUpdateEvent&)> s_Delegate(this, &Editor::OnFrameUpdate);
    Globals::GameLoopManager->RegisterFrameUpdate(s_Delegate, 1, EUpdateMode::eUpdateAlways);
}

bool Editor::ImGuiCopyWidget(const std::string& p_Id) {
    ImGui::SameLine(0, 10.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, {0.5, 0.5});
    ImGui::SetWindowFontScale(0.6);

    const auto s_Result = ImGui::Button((std::string(ICON_MD_CONTENT_COPY) + "##" + p_Id).c_str(), {20, 20});

    ImGui::SetWindowFontScale(1.0);
    ImGui::PopStyleVar(2);

    return s_Result;
}

std::string BehaviorToString(ECompiledBehaviorType p_Type) {
    switch (p_Type) {
        case ECompiledBehaviorType::BT_ConditionScope: return "BT_ConditionScope";
        case ECompiledBehaviorType::BT_Random: return "BT_Random";
        case ECompiledBehaviorType::BT_Match: return "BT_Match";
        case ECompiledBehaviorType::BT_Sequence: return "BT_Sequence";
        case ECompiledBehaviorType::BT_Dummy: return "BT_Dummy";
        case ECompiledBehaviorType::BT_Dummy2: return "BT_Dummy2";
        case ECompiledBehaviorType::BT_Error: return "BT_Error";
        case ECompiledBehaviorType::BT_Wait: return "BT_Wait";
        case ECompiledBehaviorType::BT_WaitForStanding: return "BT_WaitForStanding";
        case ECompiledBehaviorType::BT_WaitBasedOnDistanceToTarget: return "BT_WaitBasedOnDistanceToTarget";
        case ECompiledBehaviorType::BT_WaitForItemHandled: return "BT_WaitForItemHandled";
        case ECompiledBehaviorType::BT_AbandonOrder: return "BT_AbandonOrder";
        case ECompiledBehaviorType::BT_CompleteOrder: return "BT_CompleteOrder";
        case ECompiledBehaviorType::BT_PlayAct: return "BT_PlayAct";
        case ECompiledBehaviorType::BT_ConfiguredAct: return "BT_ConfiguredAct";
        case ECompiledBehaviorType::BT_PlayReaction: return "BT_PlayReaction";
        case ECompiledBehaviorType::BT_SimpleReaction: return "BT_SimpleReaction";
        case ECompiledBehaviorType::BT_SituationAct: return "BT_SituationAct";
        case ECompiledBehaviorType::BT_SituationApproach: return "BT_SituationApproach";
        case ECompiledBehaviorType::BT_SituationGetHelp: return "BT_SituationGetHelp";
        case ECompiledBehaviorType::BT_SituationFace: return "BT_SituationFace";
        case ECompiledBehaviorType::BT_SituationConversation: return "BT_SituationConversation";
        case ECompiledBehaviorType::BT_Holster: return "BT_Holster";
        case ECompiledBehaviorType::BT_SpeakWait: return "BT_SpeakWait";
        case ECompiledBehaviorType::BT_SpeakWaitWithFallbackIfAlone: return "BT_SpeakWaitWithFallbackIfAlone";
        case ECompiledBehaviorType::BT_ConfiguredSpeak: return "BT_ConfiguredSpeak";
        case ECompiledBehaviorType::BT_ConditionedConfiguredSpeak: return "BT_ConditionedConfiguredSpeak";
        case ECompiledBehaviorType::BT_ConditionedConfiguredAct: return "BT_ConditionedConfiguredAct";
        case ECompiledBehaviorType::BT_SpeakCustomOrDefaultDistractionAckSoundDef: return
                    "BT_SpeakCustomOrDefaultDistractionAckSoundDef";
        case ECompiledBehaviorType::BT_SpeakCustomOrDefaultDistractionInvestigationSoundDef: return
                    "BT_SpeakCustomOrDefaultDistractionInvestigationSoundDef";
        case ECompiledBehaviorType::BT_SpeakCustomOrDefaultDistractionStndSoundDef: return
                    "BT_SpeakCustomOrDefaultDistractionStndSoundDef";
        case ECompiledBehaviorType::BT_Pickup: return "BT_Pickup";
        case ECompiledBehaviorType::BT_Drop: return "BT_Drop";
        case ECompiledBehaviorType::BT_PlayConversation: return "BT_PlayConversation";
        case ECompiledBehaviorType::BT_PlayAnimation: return "BT_PlayAnimation";
        case ECompiledBehaviorType::BT_MoveToLocation: return "BT_MoveToLocation";
        case ECompiledBehaviorType::BT_MoveToTargetKnownPosition: return "BT_MoveToTargetKnownPosition";
        case ECompiledBehaviorType::BT_MoveToTargetActualPosition: return "BT_MoveToTargetActualPosition";
        case ECompiledBehaviorType::BT_MoveToInteraction: return "BT_MoveToInteraction";
        case ECompiledBehaviorType::BT_MoveToNPC: return "BT_MoveToNPC";
        case ECompiledBehaviorType::BT_FollowTargetKnownPosition: return "BT_FollowTargetKnownPosition";
        case ECompiledBehaviorType::BT_FollowTargetActualPosition: return "BT_FollowTargetActualPosition";
        case ECompiledBehaviorType::BT_PickUpItem: return "BT_PickUpItem";
        case ECompiledBehaviorType::BT_GrabItem: return "BT_GrabItem";
        case ECompiledBehaviorType::BT_PutDownItem: return "BT_PutDownItem";
        case ECompiledBehaviorType::BT_Search: return "BT_Search";
        case ECompiledBehaviorType::BT_LimitedSearch: return "BT_LimitedSearch";
        case ECompiledBehaviorType::BT_MoveTo: return "BT_MoveTo";
        case ECompiledBehaviorType::BT_Reposition: return "BT_Reposition";
        case ECompiledBehaviorType::BT_SituationMoveTo: return "BT_SituationMoveTo";
        case ECompiledBehaviorType::BT_FormationMove: return "BT_FormationMove";
        case ECompiledBehaviorType::BT_SituationJumpTo: return "BT_SituationJumpTo";
        case ECompiledBehaviorType::BT_AmbientWalk: return "BT_AmbientWalk";
        case ECompiledBehaviorType::BT_AmbientStand: return "BT_AmbientStand";
        case ECompiledBehaviorType::BT_CrowdAmbientStand: return "BT_CrowdAmbientStand";
        case ECompiledBehaviorType::BT_AmbientItemUse: return "BT_AmbientItemUse";
        case ECompiledBehaviorType::BT_AmbientLook: return "BT_AmbientLook";
        case ECompiledBehaviorType::BT_Act: return "BT_Act";
        case ECompiledBehaviorType::BT_Patrol: return "BT_Patrol";
        case ECompiledBehaviorType::BT_MoveToPosition: return "BT_MoveToPosition";
        case ECompiledBehaviorType::BT_AlertedStand: return "BT_AlertedStand";
        case ECompiledBehaviorType::BT_AlertedDebug: return "BT_AlertedDebug";
        case ECompiledBehaviorType::BT_AttentionToPerson: return "BT_AttentionToPerson";
        case ECompiledBehaviorType::BT_StunnedByFlashGrenade: return "BT_StunnedByFlashGrenade";
        case ECompiledBehaviorType::BT_CuriousIdle: return "BT_CuriousIdle";
        case ECompiledBehaviorType::BT_InvestigateWeapon: return "BT_InvestigateWeapon";
        case ECompiledBehaviorType::BT_DeliverWeapon: return "BT_DeliverWeapon";
        case ECompiledBehaviorType::BT_RecoverUnconscious: return "BT_RecoverUnconscious";
        case ECompiledBehaviorType::BT_GetOutfit: return "BT_GetOutfit";
        case ECompiledBehaviorType::BT_RadioCall: return "BT_RadioCall";
        case ECompiledBehaviorType::BT_EscortOut: return "BT_EscortOut";
        case ECompiledBehaviorType::BT_StashItem: return "BT_StashItem";
        case ECompiledBehaviorType::BT_CautiousSearchPosition: return "BT_CautiousSearchPosition";
        case ECompiledBehaviorType::BT_LockdownWarning: return "BT_LockdownWarning";
        case ECompiledBehaviorType::BT_WakeUpUnconscious: return "BT_WakeUpUnconscious";
        case ECompiledBehaviorType::BT_DeadBodyInvestigate: return "BT_DeadBodyInvestigate";
        case ECompiledBehaviorType::BT_GuardDeadBody: return "BT_GuardDeadBody";
        case ECompiledBehaviorType::BT_DragDeadBody: return "BT_DragDeadBody";
        case ECompiledBehaviorType::BT_CuriousBystander: return "BT_CuriousBystander";
        case ECompiledBehaviorType::BT_DeadBodyBystander: return "BT_DeadBodyBystander";
        case ECompiledBehaviorType::BT_StandOffArrest: return "BT_StandOffArrest";
        case ECompiledBehaviorType::BT_StandOffReposition: return "BT_StandOffReposition";
        case ECompiledBehaviorType::BT_StandAndAim: return "BT_StandAndAim";
        case ECompiledBehaviorType::BT_CloseCombat: return "BT_CloseCombat";
        case ECompiledBehaviorType::BT_MoveToCloseCombat: return "BT_MoveToCloseCombat";
        case ECompiledBehaviorType::BT_MoveAwayFromCloseCombat: return "BT_MoveAwayFromCloseCombat";
        case ECompiledBehaviorType::BT_CoverFightSeasonTwo: return "BT_CoverFightSeasonTwo";
        case ECompiledBehaviorType::BT_ShootFromPosition: return "BT_ShootFromPosition";
        case ECompiledBehaviorType::BT_StandAndShoot: return "BT_StandAndShoot";
        case ECompiledBehaviorType::BT_CheckLastPosition: return "BT_CheckLastPosition";
        case ECompiledBehaviorType::BT_ProtoSearchIdle: return "BT_ProtoSearchIdle";
        case ECompiledBehaviorType::BT_ProtoApproachSearchArea: return "BT_ProtoApproachSearchArea";
        case ECompiledBehaviorType::BT_ProtoSearchPosition: return "BT_ProtoSearchPosition";
        case ECompiledBehaviorType::BT_ShootTarget: return "BT_ShootTarget";
        case ECompiledBehaviorType::BT_TriggerAlarm: return "BT_TriggerAlarm";
        case ECompiledBehaviorType::BT_MoveInCover: return "BT_MoveInCover";
        case ECompiledBehaviorType::BT_MoveToCover: return "BT_MoveToCover";
        case ECompiledBehaviorType::BT_HomeAttackOrigin: return "BT_HomeAttackOrigin";
        case ECompiledBehaviorType::BT_Shoot: return "BT_Shoot";
        case ECompiledBehaviorType::BT_Aim: return "BT_Aim";
        case ECompiledBehaviorType::BT_MoveToRandomNeighbourNode: return "BT_MoveToRandomNeighbourNode";
        case ECompiledBehaviorType::BT_MoveToRandomNeighbourNodeAiming: return "BT_MoveToRandomNeighbourNodeAiming";
        case ECompiledBehaviorType::BT_MoveToAndPlayCombatPositionAct: return "BT_MoveToAndPlayCombatPositionAct";
        case ECompiledBehaviorType::BT_MoveToAimingAndPlayCombatPositionAct: return
                    "BT_MoveToAimingAndPlayCombatPositionAct";
        case ECompiledBehaviorType::BT_PlayJumpyReaction: return "BT_PlayJumpyReaction";
        case ECompiledBehaviorType::BT_JumpyInvestigation: return "BT_JumpyInvestigation";
        case ECompiledBehaviorType::BT_AgitatedPatrol: return "BT_AgitatedPatrol";
        case ECompiledBehaviorType::BT_AgitatedGuard: return "BT_AgitatedGuard";
        case ECompiledBehaviorType::BT_HeroEscort: return "BT_HeroEscort";
        case ECompiledBehaviorType::BT_Escort: return "BT_Escort";
        case ECompiledBehaviorType::BT_ControlledFormationMove: return "BT_ControlledFormationMove";
        case ECompiledBehaviorType::BT_EscortSearch: return "BT_EscortSearch";
        case ECompiledBehaviorType::BT_LeadEscort: return "BT_LeadEscort";
        case ECompiledBehaviorType::BT_LeadEscort2: return "BT_LeadEscort2";
        case ECompiledBehaviorType::BT_AimReaction: return "BT_AimReaction";
        case ECompiledBehaviorType::BT_FollowHitman: return "BT_FollowHitman";
        case ECompiledBehaviorType::BT_RideTheLightning: return "BT_RideTheLightning";
        case ECompiledBehaviorType::BT_Scared: return "BT_Scared";
        case ECompiledBehaviorType::BT_Flee: return "BT_Flee";
        case ECompiledBehaviorType::BT_AgitatedBystander: return "BT_AgitatedBystander";
        case ECompiledBehaviorType::BT_SentryFrisk: return "BT_SentryFrisk";
        case ECompiledBehaviorType::BT_SentryIdle: return "BT_SentryIdle";
        case ECompiledBehaviorType::BT_SentryWarning: return "BT_SentryWarning";
        case ECompiledBehaviorType::BT_SentryCheckItem: return "BT_SentryCheckItem";
        case ECompiledBehaviorType::BT_VIPScared: return "BT_VIPScared";
        case ECompiledBehaviorType::BT_VIPSafeRoomTrespasser: return "BT_VIPSafeRoomTrespasser";
        case ECompiledBehaviorType::BT_DefendVIP: return "BT_DefendVIP";
        case ECompiledBehaviorType::BT_CautiousVIP: return "BT_CautiousVIP";
        case ECompiledBehaviorType::BT_CautiousGuardVIP: return "BT_CautiousGuardVIP";
        case ECompiledBehaviorType::BT_InfectedConfused: return "BT_InfectedConfused";
        case ECompiledBehaviorType::BT_EnterInfected: return "BT_EnterInfected";
        case ECompiledBehaviorType::BT_CureInfected: return "BT_CureInfected";
        case ECompiledBehaviorType::BT_SickActInfected: return "BT_SickActInfected";
        case ECompiledBehaviorType::BT_Smart: return "BT_Smart";
        case ECompiledBehaviorType::BT_Controlled: return "BT_Controlled";
        case ECompiledBehaviorType::BT_SpeakTest: return "BT_SpeakTest";
        case ECompiledBehaviorType::BT_Conversation: return "BT_Conversation";
        case ECompiledBehaviorType::BT_RunToHelp: return "BT_RunToHelp";
        case ECompiledBehaviorType::BT_WaitForDialog: return "BT_WaitForDialog";
        case ECompiledBehaviorType::BT_WaitForConfiguredAct: return "BT_WaitForConfiguredAct";
        case ECompiledBehaviorType::BT_TestFlashbangGrenadeThrow: return "BT_TestFlashbangGrenadeThrow";
        case ECompiledBehaviorType::BT_BEHAVIORS_END: return "BT_BEHAVIORS_END";
        case ECompiledBehaviorType::BT_RenewEvent: return "BT_RenewEvent";
        case ECompiledBehaviorType::BT_ExpireEvent: return "BT_ExpireEvent";
        case ECompiledBehaviorType::BT_ExpireEvents: return "BT_ExpireEvents";
        case ECompiledBehaviorType::BT_SetEventHandled: return "BT_SetEventHandled";
        case ECompiledBehaviorType::BT_RenewSharedEvent: return "BT_RenewSharedEvent";
        case ECompiledBehaviorType::BT_ExpireSharedEvent: return "BT_ExpireSharedEvent";
        case ECompiledBehaviorType::BT_ExpireAllEvents: return "BT_ExpireAllEvents";
        case ECompiledBehaviorType::BT_CreateOrJoinSituation: return "BT_CreateOrJoinSituation";
        case ECompiledBehaviorType::BT_JoinSituation: return "BT_JoinSituation";
        case ECompiledBehaviorType::BT_ForceActorToJoinSituation: return "BT_ForceActorToJoinSituation";
        case ECompiledBehaviorType::BT_JoinSituationWithActor: return "BT_JoinSituationWithActor";
        case ECompiledBehaviorType::BT_LeaveSituation: return "BT_LeaveSituation";
        case ECompiledBehaviorType::BT_Escalate: return "BT_Escalate";
        case ECompiledBehaviorType::BT_GotoPhase: return "BT_GotoPhase";
        case ECompiledBehaviorType::BT_RenewGoal: return "BT_RenewGoal";
        case ECompiledBehaviorType::BT_ExpireGoal: return "BT_ExpireGoal";
        case ECompiledBehaviorType::BT_RenewGoalOf: return "BT_RenewGoalOf";
        case ECompiledBehaviorType::BT_ExpireGoalOf: return "BT_ExpireGoalOf";
        case ECompiledBehaviorType::BT_SetTension: return "BT_SetTension";
        case ECompiledBehaviorType::BT_TriggerSpotted: return "BT_TriggerSpotted";
        case ECompiledBehaviorType::BT_CopyKnownLocation: return "BT_CopyKnownLocation";
        case ECompiledBehaviorType::BT_UpdateKnownLocation: return "BT_UpdateKnownLocation";
        case ECompiledBehaviorType::BT_TransferKnownObjectPositions: return "BT_TransferKnownObjectPositions";
        case ECompiledBehaviorType::BT_WitnessAttack: return "BT_WitnessAttack";
        case ECompiledBehaviorType::BT_Speak: return "BT_Speak";
        case ECompiledBehaviorType::BT_StartDynamicEnforcer: return "BT_StartDynamicEnforcer";
        case ECompiledBehaviorType::BT_StopDynamicEnforcer: return "BT_StopDynamicEnforcer";
        case ECompiledBehaviorType::BT_StartRangeBasedDynamicEnforcer: return "BT_StartRangeBasedDynamicEnforcer";
        case ECompiledBehaviorType::BT_StopRangeBasedDynamicEnforcerForLocation: return
                    "BT_StopRangeBasedDynamicEnforcerForLocation";
        case ECompiledBehaviorType::BT_StopRangeBasedDynamicEnforcer: return "BT_StopRangeBasedDynamicEnforcer";
        case ECompiledBehaviorType::BT_SetDistracted: return "BT_SetDistracted";
        case ECompiledBehaviorType::BT_IgnoreAllDistractionsExceptTheNewest: return
                    "BT_IgnoreAllDistractionsExceptTheNewest";
        case ECompiledBehaviorType::BT_IgnoreDistractions: return "BT_IgnoreDistractions";
        case ECompiledBehaviorType::BT_PerceptibleEntityNotifyWillReact: return "BT_PerceptibleEntityNotifyWillReact";
        case ECompiledBehaviorType::BT_PerceptibleEntityNotifyReacted: return "BT_PerceptibleEntityNotifyReacted";
        case ECompiledBehaviorType::BT_PerceptibleEntityNotifyInvestigating: return
                    "BT_PerceptibleEntityNotifyInvestigating";
        case ECompiledBehaviorType::BT_PerceptibleEntityNotifyInvestigated: return
                    "BT_PerceptibleEntityNotifyInvestigated";
        case ECompiledBehaviorType::BT_PerceptibleEntityNotifyTerminate: return "BT_PerceptibleEntityNotifyTerminate";
        case ECompiledBehaviorType::BT_LeaveDistractionAssistantRole: return "BT_LeaveDistractionAssistantRole";
        case ECompiledBehaviorType::BT_LeaveDistractionAssitingGuardRole: return "BT_LeaveDistractionAssitingGuardRole";
        case ECompiledBehaviorType::BT_RequestSuitcaseAssistanceOverRadio: return
                    "BT_RequestSuitcaseAssistanceOverRadio";
        case ECompiledBehaviorType::BT_RequestSuitcaseAssistanceFaceToFace: return
                    "BT_RequestSuitcaseAssistanceFaceToFace";
        case ECompiledBehaviorType::BT_ExpireArrestReasons: return "BT_ExpireArrestReasons";
        case ECompiledBehaviorType::BT_SetDialogSwitch_NPCID: return "BT_SetDialogSwitch_NPCID";
        case ECompiledBehaviorType::BT_InfectedAssignToFollowPlayer: return "BT_InfectedAssignToFollowPlayer";
        case ECompiledBehaviorType::BT_InfectedRemoveFromFollowPlayer: return "BT_InfectedRemoveFromFollowPlayer";
        case ECompiledBehaviorType::BT_Log: return "BT_Log";
        case ECompiledBehaviorType::BT_COMMANDS_END: return "BT_COMMANDS_END";
        case ECompiledBehaviorType::BT_Invalid: return "BT_Invalid";
        default: return "<unknown>";
    }
}

void Editor::OnDrawUI(bool p_HasFocus) {
    auto s_ImgGuiIO = ImGui::GetIO();

    if (!m_ShowUI) return;

    DrawEntityTree();
    DrawEntityProperties();
    DrawEntityManipulator(p_HasFocus);
    //DrawPinTracer();

    if (m_CameraRT && m_Camera) {
        ImGui::Begin("RT Texture");

        const auto s_CameraRTEntity = m_CameraRT.QueryInterface<ZRenderDestinationTextureEntity>();
        const auto s_RT = reinterpret_cast<ZRenderDestination*>(s_CameraRTEntity->GetRenderDestination());

        m_CameraRT.SetProperty("m_bVisible", true);
        m_Camera.SetProperty("m_bVisible", true);

        if (s_RT)
            SDK()->ImGuiGameRenderTarget(s_RT);

        ImGui::End();
    }

    /*ImGui::PushFont(SDK()->GetImGuiBlackFont());
    const auto s_Expanded = ImGui::Begin("Behaviors");
    ImGui::PushFont(SDK()->GetImGuiRegularFont());

    if (s_Expanded)
    {
        for (int i = 0; i < *Globals::NextActorId; ++i)
        {
            const auto& s_Actor = Globals::ActorManager->m_aActiveActors[i];

            const auto s_ActorSpatial = s_Actor.m_ref.QueryInterface<ZSpatialEntity>();

            if (!s_ActorSpatial)
                continue;

            std::string s_BehaviorName = "<none>";

            if (s_Actor.m_pInterfaceRef->m_nCurrentBehaviorIndex >= 0)
            {
                auto& s_BehaviorData = Globals::BehaviorService->m_aKnowledgeData[s_Actor.m_pInterfaceRef->m_nCurrentBehaviorIndex];

                if (s_BehaviorData.m_pCurrentBehavior)
                    s_BehaviorName = BehaviorToString(static_cast<ECompiledBehaviorType>(s_BehaviorData.m_pCurrentBehavior->m_Type));
            }

            ImGui::Text(fmt::format("{} => {}", s_Actor.m_pInterfaceRef->m_sActorName, s_BehaviorName).c_str());
        }
    }

    ImGui::PopFont();
    ImGui::End();
    ImGui::PopFont();*/
}

void Editor::OnFrameUpdate(const SGameUpdateEvent& p_UpdateEvent) {}

void Editor::OnMouseDown(SVector2 p_Pos, bool p_FirstClick) {
    SVector3 s_World;
    SVector3 s_Direction;
    SDK()->ScreenToWorld(p_Pos, s_World, s_Direction);

    float4 s_DirectionVec(s_Direction.x, s_Direction.y, s_Direction.z, 1.f);

    float4 s_From = float4(s_World.x, s_World.y, s_World.z, 1.f);
    float4 s_To = s_From + (s_DirectionVec * 200.f);

    if (!*Globals::CollisionManager) {
        Logger::Error("Collision manager not found.");
        return;
    }

    ZRayQueryInput s_RayInput {
        .m_vFrom = s_From,
        .m_vTo = s_To,
    };

    ZRayQueryOutput s_RayOutput {};

    if(m_raycastLogging) {
        Logger::Debug("RayCasting from {} to {}.", s_From, s_To);
    }

    if (!(*Globals::CollisionManager)->RayCastClosestHit(s_RayInput, &s_RayOutput)) {
        if(m_raycastLogging){
            Logger::Error("Raycast failed.");
        }
        return;
    }

    if(m_raycastLogging) {
        Logger::Debug("Raycast result: {} {}", fmt::ptr(&s_RayOutput), s_RayOutput.m_vPosition);
    }

    m_From = s_From;
    m_To = s_To;
    m_Hit = s_RayOutput.m_vPosition;
    m_Normal = s_RayOutput.m_vNormal;

    if (p_FirstClick) {
        if (s_RayOutput.m_BlockingEntity) {
            const auto& s_Interfaces = *s_RayOutput.m_BlockingEntity->GetType()->m_pInterfaces;
            Logger::Trace(
                "Hit entity of type '{}' with id '{:x}'.", s_Interfaces[0].m_pTypeId->typeInfo()->m_pTypeName,
                s_RayOutput.m_BlockingEntity->GetType()->m_nEntityId
            );

            const auto s_SceneCtx = Globals::Hitman5Module->m_pEntitySceneContext;
            ZEntityRef s_SelectedEntity = s_RayOutput.m_BlockingEntity;

            for (int i = 0; i < s_SceneCtx->m_aLoadedBricks.size(); ++i) {
                const auto& s_Brick = s_SceneCtx->m_aLoadedBricks[i];

                if (s_SelectedEntity.IsAnyParent(s_Brick.entityRef)) {
                    Logger::Debug("Found entity in brick {} (idx = {}).", s_Brick.runtimeResourceID, i);
                    m_SelectedBrickIndex = i;
                    break;
                }
            }

            OnSelectEntity(s_SelectedEntity, std::nullopt);
        }
    }
}

void Editor::SpawnCameras() {
    auto s_Scene = Globals::Hitman5Module->m_pEntitySceneContext->m_pScene;

    if (!s_Scene) {
        Logger::Error("Scene is not yet loaded. Cannot spawn editor cameras.");
        return;
    }

    {
        TResourcePtr<ZTemplateEntityFactory> s_CameraResource;
        Globals::ResourceManager->GetResourcePtr(
            s_CameraResource, ResId<"[assembly:/_sdk/editor/editor_camera.brick].pc_entitytype">, 0
        );

        if (!s_CameraResource) {
            Logger::Error("Could not get editor camera resource. Is the editor brick loaded?");
            return;
        }

        Functions::ZEntityManager_NewEntity->Call(
            Globals::EntityManager, m_Camera, "SDKCam", s_CameraResource, s_Scene.m_ref, nullptr, -1
        );

        if (!m_Camera) {
            Logger::Error("Could not spawn editor camera entity.");
            return;
        }
    }

    {
        TResourcePtr<ZTemplateEntityFactory> s_RTResource;
        Globals::ResourceManager->GetResourcePtr(
            s_RTResource, ResId<"[assembly:/_sdk/editor/camera_texture.brick].pc_entitytype">, 0
        );

        if (!s_RTResource) {
            Logger::Error("Could not get editor camera texture resource. Is the editor brick loaded?");
            return;
        }

        Functions::ZEntityManager_NewEntity->Call(
            Globals::EntityManager, m_CameraRT, "SDKCamRT", s_RTResource, s_Scene.m_ref, nullptr, -1
        );

        if (!m_CameraRT) {
            Logger::Error("Could not spawn editor camera texture entity.");
            return;
        }
    }

    const auto s_Camera = m_Camera.QueryInterface<ZCameraEntity>();
    Logger::Debug("Spawned camera = {}", fmt::ptr(s_Camera));

    const auto s_CurrentCamera = Functions::GetCurrentCamera->Call();
    s_Camera->SetWorldMatrix(s_CurrentCamera->GetWorldMatrix());

    const auto s_CameraRT = m_CameraRT.QueryInterface<ZRenderDestinationTextureEntity>();
    Logger::Debug(
        "Spawned rt = {} sources = {} source = {}", fmt::ptr(s_CameraRT), s_CameraRT->m_aMultiSource.size(),
        s_CameraRT->m_nSelectedSource
    );

    s_CameraRT->SetSource(&m_Camera);

    Logger::Debug(
        "Added source to rt = {} sources = {} source = {}", fmt::ptr(s_CameraRT), s_CameraRT->m_aMultiSource.size(),
        s_CameraRT->m_nSelectedSource
    );
}

void Editor::ActivateCamera(ZEntityRef* m_CameraEntity) {
    TEntityRef<IRenderDestinationEntity> s_RenderDest;
    Functions::ZCameraManager_GetActiveRenderDestinationEntity->Call(Globals::CameraManager, &s_RenderDest);

    m_OriginalCam = *s_RenderDest.m_pInterfaceRef->GetSource();

    s_RenderDest.m_pInterfaceRef->SetSource(m_CameraEntity);
}

void Editor::DeactivateCamera() {
    TEntityRef<IRenderDestinationEntity> s_RenderDest;
    Functions::ZCameraManager_GetActiveRenderDestinationEntity->Call(Globals::CameraManager, &s_RenderDest);

    s_RenderDest.m_pInterfaceRef->SetSource(&m_OriginalCam);
}

QneTransform Editor::MatrixToQneTransform(const SMatrix& p_Matrix) {
    // This is adapted from QN: https://github.com/atampy25/quickentity-rs/blob/240ffba9d23dedc864bd39f1f029646837d3916d/src/lib.rs#L2528
    auto s_Trans = p_Matrix;

    constexpr float c_RAD2DEG = 180.0f / std::numbers::pi;

    const auto n11 = s_Trans.XAxis.x;
    const auto n12 = s_Trans.XAxis.y;
    const auto n13 = s_Trans.XAxis.z;
    const auto n14 = 0.0f;
    const auto n21 = s_Trans.YAxis.x;
    const auto n22 = s_Trans.YAxis.y;
    const auto n23 = s_Trans.YAxis.z;
    const auto n24 = 0.0f;
    const auto n31 = s_Trans.ZAxis.x;
    const auto n32 = s_Trans.ZAxis.y;
    const auto n33 = s_Trans.ZAxis.z;
    const auto n34 = 0.0f;
    const auto n41 = s_Trans.Trans.x;
    const auto n42 = s_Trans.Trans.y;
    const auto n43 = s_Trans.Trans.z;
    const auto n44 = 1.0f;

    const auto det =
            n41 * (n14 * n23 * n32 - n13 * n24 * n32 - n14 * n22 * n33 + n12 * n24 * n33 + n13 * n22 * n34
                - n12 * n23 * n34) + n42
            * (n11 * n23 * n34 - n11 * n24 * n33 + n14 * n21 * n33 - n13 * n21 * n34 + n13 * n24 * n31
                - n14 * n23 * n31) + n43
            * (n11 * n24 * n32 - n11 * n22 * n34 - n14 * n21 * n32 + n12 * n21 * n34 + n14 * n22 * n31
                - n12 * n24 * n31) + n44
            * (-n13 * n22 * n31 - n11 * n23 * n32 + n11 * n22 * n33 + n13 * n21 * n32 - n12 * n21 * n33
                + n12 * n23 * n31);

    auto sx = n11 * n11 + n21 * n21 + n31 * n31;
    const auto sy = n12 * n12 + n22 * n22 + n32 * n32;
    const auto sz = n13 * n13 + n23 * n23 + n33 * n33;

    if (det < 0.0f) {
        sx = -sx;
    }

    const auto inv_sx = 1.0f / sx;
    const auto inv_sy = 1.0f / sy;
    const auto inv_sz = 1.0f / sz;

    s_Trans.XAxis.x *= inv_sx;
    s_Trans.YAxis.x *= inv_sx;
    s_Trans.ZAxis.x *= inv_sx;
    s_Trans.XAxis.y *= inv_sy;
    s_Trans.YAxis.y *= inv_sy;
    s_Trans.ZAxis.y *= inv_sy;
    s_Trans.XAxis.z *= inv_sz;
    s_Trans.YAxis.z *= inv_sz;
    s_Trans.ZAxis.z *= inv_sz;

    float s_RotationX = abs(s_Trans.XAxis.z) < 0.9999999f
                            ? atan2f(-s_Trans.YAxis.z, s_Trans.ZAxis.z) * c_RAD2DEG
                            : atan2f(s_Trans.ZAxis.y, s_Trans.YAxis.y) * c_RAD2DEG;

    float s_RotationY = asinf(min(max(-1.f, s_Trans.XAxis.z), 1.f)) * c_RAD2DEG;

    float s_RotationZ = abs(s_Trans.XAxis.z) < 0.9999999f
                            ? atan2f(-s_Trans.XAxis.y, s_Trans.XAxis.x) * c_RAD2DEG
                            : 0.f;

    return QneTransform {
        .Position = {n41, n42, n43},
        .Rotation = {s_RotationX, s_RotationY, s_RotationZ},
        .Scale = {sx, sy, sz},
    };
}

SMatrix Editor::QneTransformToMatrix(const QneTransform& p_Transform) {
    // This is adapted from QN: https://github.com/atampy25/quickentity-rs/blob/240ffba9d23dedc864bd39f1f029646837d3916d/src/lib.rs#L2782
    constexpr float c_DEG2RAD = std::numbers::pi / 180.0f;

    const auto x = p_Transform.Rotation.x * c_DEG2RAD;
    const auto y = p_Transform.Rotation.y * c_DEG2RAD;
    const auto z = p_Transform.Rotation.z * c_DEG2RAD;

    const auto c1 = cosf(x / 2.0f);
    const auto c2 = cosf(y / 2.0f);
    const auto c3 = cosf(z / 2.0f);

    const auto s1 = sinf(x / 2.0f);
    const auto s2 = sinf(y / 2.0f);
    const auto s3 = sinf(z / 2.0f);

    const auto quat_x = s1 * c2 * c3 + c1 * s2 * s3;
    const auto quat_y = c1 * s2 * c3 - s1 * c2 * s3;
    const auto quat_z = c1 * c2 * s3 + s1 * s2 * c3;
    const auto quat_w = c1 * c2 * c3 - s1 * s2 * s3;

    const auto x2 = quat_x + quat_x;
    const auto y2 = quat_y + quat_y;
    const auto z2 = quat_z + quat_z;
    const auto xx = quat_x * x2;
    const auto xy = quat_x * y2;
    const auto xz = quat_x * z2;
    const auto yy = quat_y * y2;
    const auto yz = quat_y * z2;
    const auto zz = quat_z * z2;
    const auto wx = quat_w * x2;
    const auto wy = quat_w * y2;
    const auto wz = quat_w * z2;

    SMatrix s_Matrix;

    s_Matrix.XAxis.x = (1.0f - (yy + zz)) * p_Transform.Scale.x;
    s_Matrix.XAxis.y = (xy - wz) * p_Transform.Scale.y;
    s_Matrix.XAxis.z = (xz + wy) * p_Transform.Scale.z;

    s_Matrix.YAxis.x = (xy + wz) * p_Transform.Scale.x;
    s_Matrix.YAxis.y = (1.0f - (xx + zz)) * p_Transform.Scale.y;
    s_Matrix.YAxis.z = (yz - wx) * p_Transform.Scale.z;

    s_Matrix.ZAxis.x = (xz - wy) * p_Transform.Scale.x;
    s_Matrix.ZAxis.y = (yz + wx) * p_Transform.Scale.y;
    s_Matrix.ZAxis.z = (1.0f - (xx + yy)) * p_Transform.Scale.z;

    s_Matrix.Trans.x = p_Transform.Position.x;
    s_Matrix.Trans.y = p_Transform.Position.y;
    s_Matrix.Trans.z = p_Transform.Position.z;

    return s_Matrix;
}

DEFINE_PLUGIN_DETOUR(Editor, void, OnLoadScene, ZEntitySceneContext* th, ZSceneData& p_SceneData) {
    /*if (p_SceneData.m_sceneName == "assembly:/_PRO/Scenes/Frontend/MainMenu.entity" ||
        p_SceneData.m_sceneName == "assembly:/_PRO/Scenes/Frontend/Boot.entity")
    //	p_SceneData.m_sceneName = "assembly:/_pro/scenes/users/notex/test.entity";
        p_SceneData.m_sceneName = "assembly:/_PRO/Scenes/Missions/TheFacility/_Scene_Mission_Polarbear_Module_002_B.entity";
    //    p_SceneData.m_sceneName = "assembly:/_pro/scenes/missions/golden/mission_gecko/scene_gecko_basic.entity";*/


    if (m_SelectionForFreeCameraEditorStyleEntity) {
        m_SelectionForFreeCameraEditorStyleEntity->m_selection.clear();
    }

    m_CachedEntityTreeMutex.lock();
    m_CachedEntityTree.reset();
    m_CachedEntityTreeMutex.unlock();

    std::vector<std::string> s_Bricks;

    for (auto& s_Brick : p_SceneData.m_sceneBricks) {
        s_Bricks.push_back(s_Brick.c_str());
    }

    m_Server.OnSceneLoading(p_SceneData.m_sceneName.c_str(), s_Bricks);

    return HookResult<void>(HookAction::Continue());
}

DEFINE_PLUGIN_DETOUR(
    Editor, ZTemplateEntityBlueprintFactory*, ZTemplateEntityBlueprintFactory_ctor, ZTemplateEntityBlueprintFactory* th,
    STemplateEntityBlueprint* pTemplateEntityBlueprint, ZResourcePending& ResourcePending
) {
    //Logger::Debug("Creating Blueprint Factory {} with template {}", fmt::ptr(th), fmt::ptr(pTemplateEntityBlueprint));
    return HookResult<ZTemplateEntityBlueprintFactory*>(HookAction::Continue());
}

DEFINE_PLUGIN_DETOUR(Editor, void, OnClearScene, ZEntitySceneContext* th, bool forReload) {
    m_SelectedBrickIndex = 0;
    m_SelectedEntity = {};
    m_Camera = {};
    m_CameraRT = {};
    m_ShouldScrollToEntity = false;

    m_CachedEntityTreeMutex.lock();
    m_CachedEntityTree.reset();
    m_CachedEntityTreeMutex.unlock();

    m_Server.OnSceneClearing(forReload);

    return HookResult<void>(HookAction::Continue());
}

DEFINE_PLUGIN_DETOUR(Editor, bool, OnInputPin, ZEntityRef entity, uint32_t pinId, const ZObjectRef& data) {
    //if (entity == m_SelectedEntity)
    {
        m_FiredInputPins[pinId] = PinFireInfo {
            .m_FireTime = std::chrono::system_clock::now(),
        };
    }

    return {HookAction::Continue()};
}

DEFINE_PLUGIN_DETOUR(Editor, bool, OnOutputPin, ZEntityRef entity, uint32_t pinId, const ZObjectRef& data) {
    //if (entity == m_SelectedEntity)
    {
        m_FiredOutputPins[pinId] = PinFireInfo {
            .m_FireTime = std::chrono::system_clock::now(),
        };
    }

    return {HookAction::Continue()};
}

DEFINE_ZHM_PLUGIN(Editor);
