namespace SensibleTempering {
    class Config {
    public:
        static void LoadConfig() noexcept;

        inline static long temperingMode;

        inline static long lightArmorMult;
        inline static long lightChestMult;
        inline static long heavyArmorMult;
        inline static long heavyChestMult;
        inline static long oneHandedMult;
        inline static long twoHandedMult;
        inline static long bowMult;
        inline static long crossbowMult;

        inline static float armorPercentPerTier;
        inline static float weaponPercentPerTier;
    };
}  // namespace betteralttab