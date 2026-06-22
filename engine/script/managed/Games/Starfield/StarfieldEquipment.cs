namespace Recreation.Games.Starfield;

// A weapon's damage family, from its type keyword. Starfield branches a lot on
// this: EM weapons stun rather than kill, particle beams split damage, and skills
// (Ballistics, Lasers, ...) key off it.
public enum WeaponKind
{
    None,
    Ballistic,
    Laser,
    Particle,
    Electromagnetic,
    Melee,
}

// A piece of apparel's slot, from its keyword. The three armored slots stack their
// resistances in Starfield.
public enum ApparelSlot
{
    None,
    Spacesuit,
    Helmet,
    Pack,
}

// Categorises Starfield gear by the keywords on its record, the counterpart of
// Skyrim's Equipment: a weapon's damage family, an apparel piece's slot, and whether
// a form is ammunition. The keyword ids live in StarfieldForms (placeholders), so
// this is the basis for combat, skill and UI logic that branches on "is this a laser"
// or "is this the spacesuit slot". Pure keyword reads with no state.
public static class StarfieldEquipment
{
    // The weapon's damage family, or None if it carries no weapon-type keyword.
    public static WeaponKind KindOf(Form weapon)
    {
        if (Has(weapon, StarfieldForms.WeaponTypeBallistic)) return WeaponKind.Ballistic;
        if (Has(weapon, StarfieldForms.WeaponTypeLaser)) return WeaponKind.Laser;
        if (Has(weapon, StarfieldForms.WeaponTypeParticle)) return WeaponKind.Particle;
        if (Has(weapon, StarfieldForms.WeaponTypeEM)) return WeaponKind.Electromagnetic;
        if (Has(weapon, StarfieldForms.WeaponTypeMelee)) return WeaponKind.Melee;
        return WeaponKind.None;
    }

    // The apparel slot, or None if the form is not slotted armor (plain clothing).
    public static ApparelSlot SlotOf(Form apparel)
    {
        if (Has(apparel, StarfieldForms.ApparelSpacesuit)) return ApparelSlot.Spacesuit;
        if (Has(apparel, StarfieldForms.ApparelHelmet)) return ApparelSlot.Helmet;
        if (Has(apparel, StarfieldForms.ApparelPack)) return ApparelSlot.Pack;
        return ApparelSlot.None;
    }

    public static bool IsWeapon(Form form) => KindOf(form) != WeaponKind.None;
    public static bool IsApparel(Form form) => SlotOf(form) != ApparelSlot.None;
    public static bool IsAmmo(Form form) => Has(form, StarfieldForms.AmmoKeyword);

    // True for the ranged families that draw from an ammo pool (everything but melee).
    public static bool UsesAmmo(Form weapon) =>
        KindOf(weapon) is WeaponKind.Ballistic or WeaponKind.Laser
            or WeaponKind.Particle or WeaponKind.Electromagnetic;

    private static bool Has(Form form, uint keyword) => form.HasKeyword(Game.GetForm(keyword));
}
