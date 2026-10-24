#include "Config.h"
#include "SimpleIni.h"

namespace ItemBasedTempering {
    void Config::LoadConfig() noexcept
    {
        logger::info("Loading config");

        CSimpleIniA ini;
        ini.SetUnicode();
        if (ini.LoadFile(R"(.\Data\SKSE\Plugins\ItemBasedTempering.ini)") != 0) {
            logger::error("Failed to Load config file.");
            return;
        }

        temperingMode = ini.GetLongValue("General", "temperingMode");

        armorPercentPerTier = (float) ini.GetDoubleValue("ItemBasedMode", "armorPercentPerTier");
        weaponPercentPerTier = (float) ini.GetDoubleValue("ItemBasedMode", "weaponPercentPerTier");

        lightArmorMult = ini.GetLongValue("VanillaPlusMode", "lightArmorMult");
        lightChestMult = ini.GetLongValue("VanillaPlusMode", "lightChestMult");
        heavyArmorMult = ini.GetLongValue("VanillaPlusMode", "heavyArmorMult");
        heavyChestMult = ini.GetLongValue("VanillaPlusMode", "heavyChestMult");
        oneHandedMult = ini.GetLongValue("VanillaPlusMode", "oneHandedMult");
        twoHandedMult = ini.GetLongValue("VanillaPlusMode", "twoHandedMult");
        bowMult = ini.GetLongValue("VanillaPlusMode", "bowMult");
        crossbowMult = ini.GetLongValue("VanillaPlusMode", "crossbowMult");

        logger::info("Loaded config");
        logger::info("");
    }
}