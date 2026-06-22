namespace Recreation.Games.Fallout;

// Well-known Fallout4.esm form ids the gameplay layer refers to by name instead
// of a bare hex literal. These are base-game ids (load order 00); resolve them at
// runtime with Fallout.GetForm.
//
// PLACEHOLDER ids: like StarfieldForms these were not probed from the .esm, so the
// hex values are stand-ins chosen only to be distinct. Replace them with the real
// records once dumped; the systems reference the names, so a fix is one edit here.
public static class FalloutForms
{
    // The keyword marking an item as an anti-radiation cure (RadAway). Radiation
    // clears rads when one is consumed, the way Starfield's OxygenCo2 feeds on the
    // oxygen-source keyword. PLACEHOLDER.
    public const uint RadAwayKeyword = 0x0000B101;

    // The keyword marking an item as a chem (Jet, Psycho, Buffout, Mentats, ...),
    // so the addiction system knows which consumables can hook the player.
    // PLACEHOLDER.
    public const uint ChemKeyword = 0x0000B102;
}
