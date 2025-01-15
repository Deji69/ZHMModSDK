#include "DebugMod.h"

#include <Glacier/ZContentKitManager.h>
#include <Glacier/ZActor.h>
#include <Glacier/ZSpatialEntity.h>
#include <Glacier/ZAction.h>
#include <Glacier/ZItem.h>
#include <imgui_internal.h>

void DebugMod::DrawPlayerBox(bool p_HasFocus) {
    if (!p_HasFocus || !m_PlayerMenuActive) {
        return;
    }

    ZContentKitManager* s_ContentKitManager = Globals::ContentKitManager;

    auto s_LocalHitman = SDK()->GetLocalPlayer();

    ImGui::PushFont(SDK()->GetImGuiBlackFont());
    const auto s_Showing = ImGui::Begin("PLAYER", &m_PlayerMenuActive);
    ImGui::PushFont(SDK()->GetImGuiRegularFont());

    if (s_Showing) {
        if (s_LocalHitman) {
            static bool s_IsInvincible = s_LocalHitman.m_ref.GetProperty<bool>("m_bIsInvincible").Get();

            if (ImGui::Checkbox("Is Invincible", &s_IsInvincible)) {
                s_LocalHitman.m_ref.SetProperty("m_bIsInvincible", s_IsInvincible);
            }

            if (ImGui::Button("Enable Infinite Ammo")) {
                EnableInfiniteAmmo();
            }
        }

        static char s_OutfitName[2048] {""};

        ImGui::Text("Outfit");
        ImGui::SameLine();

        const bool s_IsInputTextEnterPressed = ImGui::InputText(
            "##OutfitName", s_OutfitName, sizeof(s_OutfitName), ImGuiInputTextFlags_EnterReturnsTrue
        );
        const bool s_IsInputTextActive = ImGui::IsItemActive();

        if (ImGui::IsItemActivated()) {
            ImGui::OpenPopup("##popup");
        }

        ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y));
        ImGui::SetNextWindowSize(ImVec2(ImGui::GetItemRectSize().x, 300));

        static uint8_t s_CurrentCharacterSetIndex = 0;
        static std::string s_CurrentcharSetCharacterType = "HeroA";
        static std::string s_CurrentcharSetCharacterType2 = "HeroA";
        static uint8_t s_CurrentOutfitVariationIndex = 1;

        if (ImGui::BeginPopup(
            "##popup",
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_ChildWindow
        )) {
            for (auto it = s_ContentKitManager->m_repositoryGlobalOutfitKits.begin(); it != s_ContentKitManager->
                 m_repositoryGlobalOutfitKits.end(); ++it) {
                TEntityRef<ZGlobalOutfitKit>* s_GlobalOutfitKit = &it->second;
                const char* s_OutfitName2 = s_GlobalOutfitKit->m_pInterfaceRef->m_sCommonName.c_str();

                if (!strstr(s_OutfitName2, s_OutfitName)) {
                    continue;
                }

                if (ImGui::Selectable(s_OutfitName2)) {
                    ImGui::ClearActiveID();
                    strcpy_s(s_OutfitName, s_OutfitName2);

                    EquipOutfit(
                        it->second, s_CurrentCharacterSetIndex, s_CurrentcharSetCharacterType.data(),
                        s_CurrentOutfitVariationIndex, s_LocalHitman.m_pInterfaceRef
                    );

                    m_GlobalOutfitKit = s_GlobalOutfitKit;
                }
            }

            if (s_IsInputTextEnterPressed || (!s_IsInputTextActive && !ImGui::IsWindowFocused())) {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        ImGui::Text("Character Set Index");
        ImGui::SameLine();

        if (ImGui::BeginCombo("##CharacterSetIndex", std::to_string(s_CurrentCharacterSetIndex).data())) {
            if (m_GlobalOutfitKit) {
                for (size_t i = 0; i < m_GlobalOutfitKit->m_pInterfaceRef->m_aCharSets.size(); ++i) {
                    const bool s_IsSelected = s_CurrentCharacterSetIndex == i;

                    if (ImGui::Selectable(std::to_string(i).data(), s_IsSelected)) {
                        s_CurrentCharacterSetIndex = i;

                        if (m_GlobalOutfitKit) {
                            EquipOutfit(
                                *m_GlobalOutfitKit, s_CurrentCharacterSetIndex, s_CurrentcharSetCharacterType.data(),
                                s_CurrentOutfitVariationIndex, s_LocalHitman.m_pInterfaceRef
                            );
                        }
                    }
                }
            }

            ImGui::EndCombo();
        }

        ImGui::Text("CharSet Character Type");
        ImGui::SameLine();

        if (ImGui::BeginCombo("##CharSetCharacterType", s_CurrentcharSetCharacterType.data())) {
            if (m_GlobalOutfitKit) {
                for (size_t i = 0; i < 3; ++i) {
                    const bool s_IsSelected = s_CurrentcharSetCharacterType == m_CharSetCharacterTypes[i];

                    if (ImGui::Selectable(m_CharSetCharacterTypes[i].data(), s_IsSelected)) {
                        s_CurrentcharSetCharacterType = m_CharSetCharacterTypes[i].data();

                        if (m_GlobalOutfitKit) {
                            EquipOutfit(
                                *m_GlobalOutfitKit, s_CurrentCharacterSetIndex, s_CurrentcharSetCharacterType.data(),
                                s_CurrentOutfitVariationIndex, s_LocalHitman.m_pInterfaceRef
                            );
                        }
                    }
                }
            }

            ImGui::EndCombo();
        }

        ImGui::Text("Outfit Variation");
        ImGui::SameLine();

        if (ImGui::BeginCombo("##OutfitVariation", std::to_string(s_CurrentOutfitVariationIndex).data())) {
            if (m_GlobalOutfitKit) {
                const auto s_CurrentCharacterSetIndex2 = s_CurrentCharacterSetIndex;
                const size_t s_VariationCount = m_GlobalOutfitKit->m_pInterfaceRef->m_aCharSets[
                            s_CurrentCharacterSetIndex2].m_pInterfaceRef->m_aCharacters[0].m_pInterfaceRef->
                        m_aVariations.
                        size();

                for (size_t i = 0; i < s_VariationCount; ++i) {
                    const bool s_IsSelected = s_CurrentOutfitVariationIndex == i;

                    if (ImGui::Selectable(std::to_string(i).data(), s_IsSelected)) {
                        s_CurrentOutfitVariationIndex = i;

                        if (m_GlobalOutfitKit) {
                            EquipOutfit(
                                *m_GlobalOutfitKit, s_CurrentCharacterSetIndex, s_CurrentcharSetCharacterType.data(),
                                s_CurrentOutfitVariationIndex, s_LocalHitman.m_pInterfaceRef
                            );
                        }
                    }
                }
            }

            ImGui::EndCombo();
        }

        if (m_GlobalOutfitKit) {
            ImGui::Checkbox("Weapons Allowed", &m_GlobalOutfitKit->m_pInterfaceRef->m_bWeaponsAllowed);
            ImGui::Checkbox("Authority Figure", &m_GlobalOutfitKit->m_pInterfaceRef->m_bAuthorityFigure);
        }

        ImGui::Separator();

        static char npcName[2048] {""};

        ImGui::Text("NPC Name");
        ImGui::SameLine();

        ImGui::InputText("##NPCName", npcName, sizeof(npcName));
        ImGui::SameLine();

        if (ImGui::Button("Get NPC Outfit")) {
            const ZActor* s_Actor = Globals::ActorManager->GetActorByName(npcName);

            if (s_Actor) {
                EquipOutfit(
                    s_Actor->m_rOutfit, s_Actor->m_nOutfitCharset, s_CurrentcharSetCharacterType2.data(),
                    s_Actor->m_nOutfitVariation, s_LocalHitman.m_pInterfaceRef
                );
            }
        }

        if (ImGui::Button("Get Nearest NPC's Outfit")) {
            const ZSpatialEntity* s_HitmanSpatialEntity = s_LocalHitman.m_ref.QueryInterface<ZSpatialEntity>();

            for (int i = 0; i < *Globals::NextActorId; ++i) {
                ZActor* actor = Globals::ActorManager->m_aActiveActors[i].m_pInterfaceRef;
                ZEntityRef s_Ref;

                actor->GetID(&s_Ref);

                ZSpatialEntity* s_ActorSpatialEntity = s_Ref.QueryInterface<ZSpatialEntity>();

                const SVector3 s_Temp = s_ActorSpatialEntity->m_mTransform.Trans - s_HitmanSpatialEntity->m_mTransform.
                        Trans;
                const float s_Distance = sqrt(s_Temp.x * s_Temp.x + s_Temp.y * s_Temp.y + s_Temp.z * s_Temp.z);

                if (s_Distance <= 3.0f) {
                    EquipOutfit(
                        actor->m_rOutfit, actor->m_nOutfitCharset, s_CurrentcharSetCharacterType2.data(),
                        actor->m_nOutfitVariation, s_LocalHitman.m_pInterfaceRef
                    );

                    break;
                }
            }
        }

        ImGui::Text("CharSet Character Type");
        ImGui::SameLine();

        if (ImGui::BeginCombo("##CharSetCharacterType2", s_CurrentcharSetCharacterType2.data())) {
            if (m_GlobalOutfitKit) {
                for (size_t i = 0; i < 3; ++i) {
                    const bool s_IsSelected = s_CurrentcharSetCharacterType2 == m_CharSetCharacterTypes[i];

                    if (ImGui::Selectable(m_CharSetCharacterTypes[i].data(), s_IsSelected)) {
                        s_CurrentcharSetCharacterType2 = m_CharSetCharacterTypes[i];
                    }
                }
            }

            ImGui::EndCombo();
        }

        ImGui::Separator();

        if (ImGui::Button("Teleport All Items To Player")) {
            auto s_HitmanSpatial = s_LocalHitman.m_ref.QueryInterface<ZSpatialEntity>();
            const ZHM5ActionManager* s_Hm5ActionManager = Globals::HM5ActionManager;

            for (size_t i = 0; i < s_Hm5ActionManager->m_Actions.size(); ++i) {
                const ZHM5Action* s_Action = s_Hm5ActionManager->m_Actions[i];

                if (s_Action->m_eActionType == EActionType::AT_PICKUP) {
                    const ZHM5Item* s_Item = s_Action->m_Object.QueryInterface<ZHM5Item>();

                    s_Item->m_rGeomentity.m_pInterfaceRef->SetWorldMatrix(s_HitmanSpatial->GetWorldMatrix());
                }
            }
        }

        if (ImGui::Button("Teleport All NPCs To Player")) {
            const auto s_HitmanSpatialEntity = s_LocalHitman.m_ref.QueryInterface<ZSpatialEntity>();

            for (size_t i = 0; i < *Globals::NextActorId; ++i) {
                ZActor* s_Actor = Globals::ActorManager->m_aActiveActors[i].m_pInterfaceRef;
                ZEntityRef s_Ref;

                s_Actor->GetID(&s_Ref);

                ZSpatialEntity* s_ActorSpatialEntity = s_Ref.QueryInterface<ZSpatialEntity>();

                s_ActorSpatialEntity->SetWorldMatrix(s_HitmanSpatialEntity->GetWorldMatrix());
            }
        }
    }

    ImGui::PopFont();
    ImGui::End();
    ImGui::PopFont();
}
