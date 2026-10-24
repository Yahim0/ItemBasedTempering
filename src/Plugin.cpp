using namespace SKSE;
using namespace SKSE::log;
using namespace SKSE::stl;

#include "Plugin.h"
#include "Config.h"

namespace ItemBasedTempering {
    std::optional<std::filesystem::path> getLogDirectory() {
        using namespace std::filesystem;
        PWSTR buf;
        SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &buf);
        std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> documentsPath{buf, CoTaskMemFree};
        path directory{documentsPath.get()};
        directory.append("My Games"sv);

        if (exists("steam_api64.dll"sv)) {
            if (exists("openvr_api.dll") || exists("Data/SkyrimVR.esm")) {
                directory.append("Skyrim VR"sv);
            } else {
                directory.append("Skyrim Special Edition"sv);
            }
        } else if (exists("Galaxy64.dll"sv)) {
            directory.append("Skyrim Special Edition GOG"sv);
        } else if (exists("eossdk-win64-shipping.dll"sv)) {
            directory.append("Skyrim Special Edition EPIC"sv);
        } else {
            return current_path().append("skselogs");
        }
        return directory.append("SKSE"sv).make_preferred();
    }

    void initializeLogging() {
        auto path = getLogDirectory();
        if (!path) {
            report_and_fail("Can't find SKSE log directory");
        }
        *path /= std::format("{}.log"sv, Plugin::Name);

        std::shared_ptr<spdlog::logger> log;
        if (IsDebuggerPresent()) {
            log = std::make_shared<spdlog::logger>("Global", std::make_shared<spdlog::sinks::msvc_sink_mt>());
        } else {
            log = std::make_shared<spdlog::logger>("Global", std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true));
        }
        log->set_level(spdlog::level::info);
        log->flush_on(spdlog::level::info);

        spdlog::set_default_logger(std::move(log));
        spdlog::set_pattern(PLUGIN_LOGPATTERN_DEFAULT);
    }
}  // namespace plugin

using namespace ItemBasedTempering;

class TemperHooks {

    static float getArBonusItemBased(float temperTier, RE::TESObjectARMO* armor) {
        if (!armor) {
            return oldGetArBonus(temperTier, armor);
        }
        return armor->GetArmorRating() * (temperTier - 1) * Config::armorPercentPerTier / 10;
    }

    static float getArBonusFallback(float temperTier, RE::TESObjectARMO* armor) {
        if (!armor) {
            return oldGetArBonus(temperTier, armor);
        }
        float ibtBonus = armor->GetArmorRating() * (temperTier - 1) * Config::armorPercentPerTier / 10;
        float vpBonus = 0;
        if (armor->IsHeavyArmor()) {
            if (armor->HasKeywordID(442604)) {  // if is chestplate
                vpBonus = oldGetArBonus(temperTier, armor) / 2 * Config::heavyChestMult;
            } else {
                vpBonus = oldGetArBonus(temperTier, armor) * Config::heavyChestMult;
            }
        } else if (armor->IsLightArmor()) {
            if (armor->HasKeywordID(442604)) {  // if is chestplate
                vpBonus = oldGetArBonus(temperTier, armor) / 2 * Config::lightArmorMult;
            } else {
                vpBonus = oldGetArBonus(temperTier, armor) * Config::lightArmorMult;
            }
        }
        return std::max(vpBonus, ibtBonus);
    }

    static float getArBonusVanillaPlus(float temperTier, RE::TESObjectARMO* armor) {
        if (!armor) {
            return oldGetArBonus(temperTier, armor);
        }
        if (armor->IsHeavyArmor()) {
            if (armor->HasKeywordID(442604)) {  // if is chestplate
                return oldGetArBonus(temperTier, armor) / 2 * Config::heavyChestMult;
            }
            return oldGetArBonus(temperTier, armor) * Config::heavyArmorMult;
        } else if (armor->IsLightArmor()) {
            if (armor->HasKeywordID(442604)) {  // if is chestplate
                return oldGetArBonus(temperTier, armor) / 2 * Config::lightChestMult;
            }
            return oldGetArBonus(temperTier, armor) * Config::lightArmorMult;
        }
        // Lets hope this doesn't happen
        return oldGetArBonus(temperTier, armor);
    }

    // executes the original getDmgBonus(), which only takes one argument
    // since it doesnt take a weapon pointer for me to know the base damage value
    // I had to take the long way and replace the calls for the calling function
    static float getDmgBonus(float temperTier) {
        REL::Relocation<decltype(getDmgBonus)> oldGetDmgBonus(REL::ID(RELOCATION_ID(25915, 26498)));
        return oldGetDmgBonus(temperTier);
    }

    static float getClampedActorValue(RE::ActorValueOwner* actorValueOwner, stl::enumeration<RE::ActorValue, std::uint32_t> q2) {
        REL::Relocation<decltype(getClampedActorValue)> oldGetClampedActorValue(REL::ID(RELOCATION_ID(26616, 27284)));
        return oldGetClampedActorValue(actorValueOwner, q2);
    }

    static float getDamageItemBased(RE::ActorValueOwner* actorValueOwner, RE::TESObjectWEAP* weapon, RE::TESAmmo* munition, float temperTier,
                           float skillMultiplier, char idkChar) {
        float oldReturn = oldGetDamage(actorValueOwner, weapon, munition, temperTier, skillMultiplier, idkChar);
        if (!actorValueOwner || !weapon || weapon->GetWeaponType() == 0 || weapon->GetWeaponType() == 8) {
            return oldReturn;
        }
        RE::TESAmmo* ammoType = munition;
        float ammoDamage = 0.0f;
        if (!munition && idkChar && actorValueOwner->GetIsPlayerOwner()) {
            ammoType = RE::PlayerCharacter::GetSingleton()->GetCurrentAmmo();
        }
        if (ammoType) {
            ammoDamage = ammoType->GetRuntimeData().data.damage;
        }
        float damage = (ammoDamage + weapon->GetAttackDamage()) *
                       RE::GameSettingCollection::GetSingleton()->GetSetting("fDamageWeaponMult")->GetFloat();
        stl::enumeration<RE::ActorValue, std::uint8_t> weaponSkill = weapon->weaponData.skill;

        // calculate temper bonus (Item Based Mode)
        float temperDamageBonus = weapon->GetAttackDamage() * (temperTier - 1) * Config::weaponPercentPerTier / 10;

        float meleeDamage = 0;
        if (weapon->GetWeaponType() >= 1 and weapon->GetWeaponType() <= 6) {  //if is mellee
            meleeDamage = actorValueOwner->GetActorValue(RE::ActorValue::kMeleeDamage);
        }
        float clampedActorValue;
        float damageSkill;
        if (weaponSkill.underlying() > 23) /*if is not valid skill*/ {
            clampedActorValue = 100;
            damageSkill = 1.0f;
        } else {
            clampedActorValue = getClampedActorValue(actorValueOwner, weaponSkill);
            if (actorValueOwner->GetIsPlayerOwner()) {
                damageSkill = (RE::GameSettingCollection::GetSingleton()->GetSetting("fDamagePCSkillMax")->GetFloat() -
                               RE::GameSettingCollection::GetSingleton()->GetSetting("fDamagePCSkillMin")->GetFloat()) *
                                  clampedActorValue * 0.01f +
                              RE::GameSettingCollection::GetSingleton()->GetSetting("fDamagePCSkillMin")->GetFloat();
            } else {
                damageSkill = (RE::GameSettingCollection::GetSingleton()->GetSetting("fDamageSkillMax")->GetFloat() -
                               RE::GameSettingCollection::GetSingleton()->GetSetting("fDamageSkillMin")->GetFloat()) *
                                  clampedActorValue * 0.01f +
                              RE::GameSettingCollection::GetSingleton()->GetSetting("fDamageSkillMin")->GetFloat();
            }
        }
        float time = 1.0f;
        if (weapon /*&& weapon->data->flags.underlying() != 2*/ && weapon->GetWeaponType() < 7 &&
            RE::VATS::GetSingleton()->mode !=
                RE::VATS::VATS_MODE::kNone) {  // dont know why all this, also commented condition is vestigial from fo3
            time = RE::GetSecondsSinceLastFrame();
        }
        float skillDamage = damageSkill * skillMultiplier;
        float returnValue = (((damage + temperDamageBonus) * skillDamage) + meleeDamage) * time;
        float noTemperCheck = (damage * skillDamage + meleeDamage) * time;
        // the UI/game rounds the damage to the nearest integer
        // in case the player tempers and the shown damage is the same as the normal weapon, increase the damage just enough for the integer increase
        return temperDamageBonus != 0.0f && std::roundf(returnValue) == std::roundf(noTemperCheck) ? noTemperCheck + 0.55f : returnValue;
    }

    static float getDamageFallback(RE::ActorValueOwner* actorValueOwner, RE::TESObjectWEAP* weapon, RE::TESAmmo* munition, float temperTier,
                           float skillMultiplier, char idkChar) {
        float oldReturn = oldGetDamage(actorValueOwner, weapon, munition, temperTier, skillMultiplier, idkChar);
        if (!actorValueOwner || !weapon || weapon->GetWeaponType() == 0 || weapon->GetWeaponType() == 8) {
            return oldReturn;
        }
        RE::TESAmmo* ammoType = munition;
        float ammoDamage = 0.0f;
        if (!munition && idkChar && actorValueOwner->GetIsPlayerOwner()) {
            ammoType = RE::PlayerCharacter::GetSingleton()->GetCurrentAmmo();
        }
        if (ammoType) {
            ammoDamage = ammoType->GetRuntimeData().data.damage;
        }
        float damage = (ammoDamage + weapon->GetAttackDamage()) *
                       RE::GameSettingCollection::GetSingleton()->GetSetting("fDamageWeaponMult")->GetFloat();
        stl::enumeration<RE::ActorValue, std::uint8_t> weaponSkill = weapon->weaponData.skill;

        float vpBonus = 0.0f; // calculate temper bonus (fallback mode)
        float ibtBonus = weapon->GetAttackDamage() * (temperTier - 1) * Config::weaponPercentPerTier / 10;
        if (weapon->GetWeaponType() >= 1 && weapon->GetWeaponType() <= 4) {
            vpBonus = getDmgBonus(temperTier) * Config::oneHandedMult;
        } else if (weapon->GetWeaponType() == 5 || weapon->GetWeaponType() == 6) {
            vpBonus = getDmgBonus(temperTier) * Config::twoHandedMult;
        } else if (weapon->GetWeaponType() == 7) {
            vpBonus = getDmgBonus(temperTier) * Config::bowMult;
        } else if (weapon->GetWeaponType() == 9) {
            vpBonus = getDmgBonus(temperTier) * Config::crossbowMult;
        }
        float temperDamageBonus = std::max(vpBonus, ibtBonus);

        float meleeDamage = 0;
        if (weapon->GetWeaponType() >= 1 and weapon->GetWeaponType() <= 6) {  //if is mellee
            meleeDamage = actorValueOwner->GetActorValue(RE::ActorValue::kMeleeDamage);
        }
        float clampedActorValue;
        float damageSkill;
        if (weaponSkill.underlying() > 23) /*if is not valid skill*/ {
            clampedActorValue = 100;
            damageSkill = 1.0f;
        } else {
            clampedActorValue = getClampedActorValue(actorValueOwner, weaponSkill);
            if (actorValueOwner->GetIsPlayerOwner()) {
                damageSkill = (RE::GameSettingCollection::GetSingleton()->GetSetting("fDamagePCSkillMax")->GetFloat() -
                               RE::GameSettingCollection::GetSingleton()->GetSetting("fDamagePCSkillMin")->GetFloat()) *
                                  clampedActorValue * 0.01f +
                              RE::GameSettingCollection::GetSingleton()->GetSetting("fDamagePCSkillMin")->GetFloat();
            } else {
                damageSkill = (RE::GameSettingCollection::GetSingleton()->GetSetting("fDamageSkillMax")->GetFloat() -
                               RE::GameSettingCollection::GetSingleton()->GetSetting("fDamageSkillMin")->GetFloat()) *
                                  clampedActorValue * 0.01f +
                              RE::GameSettingCollection::GetSingleton()->GetSetting("fDamageSkillMin")->GetFloat();
            }
        }
        float time = 1.0f;
        if (weapon /*&& weapon->data->flags.underlying() != 2*/ && weapon->GetWeaponType() < 7 &&
            RE::VATS::GetSingleton()->mode !=
                RE::VATS::VATS_MODE::kNone) {  // dont know why all this, also commented condition is vestigial from fo3
            time = RE::GetSecondsSinceLastFrame();
        }
        float skillDamage = damageSkill * skillMultiplier;
        return (((damage + temperDamageBonus) * skillDamage) + meleeDamage) * time;
    }

    static float getDamageVanillaPlus(RE::ActorValueOwner* actorValueOwner, RE::TESObjectWEAP* weapon, RE::TESAmmo* munition, float temperTier,
                           float skillMultiplier, char idkChar) {
        float oldReturn = oldGetDamage(actorValueOwner, weapon, munition, temperTier, skillMultiplier, idkChar);
        if (!actorValueOwner || !weapon || weapon->GetWeaponType() == 0 || weapon->GetWeaponType() == 8) {
            return oldReturn;
        }
        RE::TESAmmo* ammoType = munition;
        float ammoDamage = 0.0f;
        if (!munition && idkChar && actorValueOwner->GetIsPlayerOwner()) {
            ammoType = RE::PlayerCharacter::GetSingleton()->GetCurrentAmmo();
        }
        if (ammoType) {
            ammoDamage = ammoType->GetRuntimeData().data.damage;
        }
        float damage = (ammoDamage + weapon->GetAttackDamage()) *
                       RE::GameSettingCollection::GetSingleton()->GetSetting("fDamageWeaponMult")->GetFloat();
        stl::enumeration<RE::ActorValue, std::uint8_t> weaponSkill = weapon->weaponData.skill;

        float temperDamageBonus = 0; // calculate temper bonus (Vanilla Plus mode)
        if (weapon->GetWeaponType() >= 1 && weapon->GetWeaponType() <= 4) {
            temperDamageBonus = getDmgBonus(temperTier) * Config::oneHandedMult;
        } else if (weapon->GetWeaponType() == 5 || weapon->GetWeaponType() == 6) {
            temperDamageBonus = getDmgBonus(temperTier) * Config::twoHandedMult;
        } else if (weapon->GetWeaponType() == 7) {
            temperDamageBonus = getDmgBonus(temperTier) * Config::bowMult;
        } else if (weapon->GetWeaponType() == 9) {
            temperDamageBonus = getDmgBonus(temperTier) * Config::crossbowMult;
        }

        float meleeDamage = 0;
        if (weapon->GetWeaponType() >= 1 and weapon->GetWeaponType() <= 6) {  //if is mellee
            meleeDamage = actorValueOwner->GetActorValue(RE::ActorValue::kMeleeDamage);
        }
        float clampedActorValue;
        float damageSkill;
        if (weaponSkill.underlying() > 23) /*if is not valid skill*/ {
            clampedActorValue = 100;
            damageSkill = 1.0f;
        } else {
            clampedActorValue = getClampedActorValue(actorValueOwner, weaponSkill);
            if (actorValueOwner->GetIsPlayerOwner()) {
                damageSkill = (RE::GameSettingCollection::GetSingleton()->GetSetting("fDamagePCSkillMax")->GetFloat() -
                               RE::GameSettingCollection::GetSingleton()->GetSetting("fDamagePCSkillMin")->GetFloat()) *
                                  clampedActorValue * 0.01f +
                              RE::GameSettingCollection::GetSingleton()->GetSetting("fDamagePCSkillMin")->GetFloat();
            } else {
                damageSkill = (RE::GameSettingCollection::GetSingleton()->GetSetting("fDamageSkillMax")->GetFloat() -
                               RE::GameSettingCollection::GetSingleton()->GetSetting("fDamageSkillMin")->GetFloat()) *
                                  clampedActorValue * 0.01f +
                              RE::GameSettingCollection::GetSingleton()->GetSetting("fDamageSkillMin")->GetFloat();
            }
        }
        float time = 1.0f;
        if (weapon /*&& weapon->data->flags.underlying() != 2*/ && weapon->GetWeaponType() < 7 &&
            RE::VATS::GetSingleton()->mode !=
                RE::VATS::VATS_MODE::kNone) {  // dont know why all this, also commented condition is vestigial from fo3
            time = RE::GetSecondsSinceLastFrame();
        }
        float skillDamage = damageSkill * skillMultiplier;
        return (((damage + temperDamageBonus) * skillDamage) + meleeDamage) * time;
    }

    static void SetGMSTFloat(const char *a_setting, float a_set) {
        auto set = RE::GameSettingCollection::GetSingleton();
        if (!set) {
            return;
        }
        auto setting = set->GetSetting(a_setting);
        if (setting) {
            setting->data.f = a_set;
            logger::info("set {} to {}", a_setting, setting->data.f);
        }
    };

    static inline REL::Relocation<decltype(getArBonusItemBased)> oldGetArBonus;
    static inline REL::Relocation<decltype(getDamageItemBased)> oldGetDamage;

public:
    static void hookAE() {
        long mode = Config::temperingMode;
        void *getArBonus;
        void *getDmg;
        if (mode == 1l) {
            SetGMSTFloat("fSmithingArmorMax", 10.0f);  // Fixes issues with half-tiers when using with degradation mods. Unused otherwise.
            SetGMSTFloat("fSmithingWeaponMax", 10.0f);
            getArBonus = getArBonusItemBased;
            getDmg = getDamageItemBased;
            logger::info("Loaded ItemBasedMode");
        } else if (mode == 2l) {
            getArBonus = getArBonusVanillaPlus;
            getDmg = getDamageVanillaPlus;
            logger::info("Loaded VanillaPlusMode");
        } else if (mode == 3l) {
            getArBonus = getArBonusFallback;
            getDmg = getDamageFallback;
            logger::info("Loaded FallbackMode");
        } else {
            logger::info("temperingMode invalid. Mod disabled.");
            return;
        }
        oldGetArBonus = SKSE::GetTrampoline().write_call<5>(REL::ID(RELOCATION_ID(50455, 51360)).address() + 0x471, getArBonus);
        SKSE::GetTrampoline().write_call<5>(REL::ID(RELOCATION_ID(50455, 51360)).address() + 0x45d, getArBonus);
        SKSE::GetTrampoline().write_call<5>(REL::ID(RELOCATION_ID(15779, 16017)).address() + 0x2f, getArBonus);
        oldGetDamage = SKSE::GetTrampoline().write_call<5>(REL::ID(RELOCATION_ID(25846, 26409)).address() + 0x19, getDmg);
        SKSE::GetTrampoline().write_call<5>(REL::ID(RELOCATION_ID(25848, 26411)).address() + 0x2a, getDmg);
        SKSE::GetTrampoline().write_call<5>(REL::ID(RELOCATION_ID(39215, 40291)).address() + 0x37, getDmg);
        SKSE::GetTrampoline().write_call<5>(REL::ID(RELOCATION_ID(42920, 44100)).address() + 0x2f8, getDmg);
    }
    static void hookSEandVR() {
        long mode = Config::temperingMode;
        void *getArBonus;
        void *getDmg;
        if (mode == 1l) {
            SetGMSTFloat("fSmithingArmorMax", 10.0f);  // Fixes issues with half-tiers when using with degradation mods. Unused otherwise.
            SetGMSTFloat("fSmithingWeaponMax", 10.0f);
            getArBonus = getArBonusItemBased;
            getDmg = getDamageItemBased;
        } else if (mode == 2l) {
            getArBonus = getArBonusVanillaPlus;
            getDmg = getDamageVanillaPlus;
        } else if (mode == 3l) {
            getArBonus = getArBonusFallback;
            getDmg = getDamageFallback;
        } else {
            logger::info("temperingMode invalid. Mod disabled.");
            return;
        }
        oldGetArBonus = SKSE::GetTrampoline().write_call<5>(REL::ID(50531).address() + 0x60, getArBonus);
        SKSE::GetTrampoline().write_call<5>(REL::ID(50531).address() + 0x72, getArBonus);
        SKSE::GetTrampoline().write_call<5>(REL::ID(RELOCATION_ID(15779, 16017)).address() + 0x2f, getArBonus);
        oldGetDamage = SKSE::GetTrampoline().write_call<5>(REL::ID(RELOCATION_ID(25846, 26409)).address() + 0x19, getDmg);
        SKSE::GetTrampoline().write_call<5>(REL::ID(RELOCATION_ID(25848, 26411)).address() + 0x2a, getDmg);
        SKSE::GetTrampoline().write_call<5>(REL::ID(39215).address() + 0x2f, getDmg);
        SKSE::GetTrampoline().write_call<5>(REL::ID(42920).address() + 0x2f4, getDmg);
    }
};

extern "C" DLLEXPORT bool SKSEPlugin_Load(const LoadInterface* skse) {
    initializeLogging();
    logger::info("'{} {}' is loading, game version '{}'...", Plugin::Name, Plugin::VersionString, REL::Module::get().version().string());
    Init(skse);
    SKSE::AllocTrampoline(100);
    SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* message) {
        if (message->type == SKSE::MessagingInterface::kDataLoaded) {
            Config::LoadConfig();
            if (REL::Module::IsSE() || REL::Module::IsVR()) {
                TemperHooks::hookSEandVR();
            }
            else if (REL::Module::IsAE()) {
                TemperHooks::hookAE();
            }
        }
        });

    logger::info("{} has finished loading.", Plugin::Name);
    return true;
}