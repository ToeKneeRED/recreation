namespace Recreation;

// A magic effect (MGEF record): the atom a spell, enchantment, potion or
// ingredient applies (Restore Health, Damage Stamina, Fortify Smithing, ...).
// Used as an identity to match the effects two ingredients share, and (for
// consumables) to read which actor value it changes and in which direction.
public sealed class MagicEffect : Form
{
    private MagicEffect(ulong handle) : base(handle) { }

    public static new MagicEffect From(ulong handle) => new(handle);

    // The actor-value name this effect modifies (e.g. "Health"), or "" for an
    // effect the actor-value store does not model (light, paralysis, ...).
    public string ActorValue => Call("GetMagicEffectActorValue").AsString();

    // True if the effect damages rather than restores or fortifies its value.
    public bool IsDetrimental => Call("GetMagicEffectDetrimental").AsBool();
}

// One magic effect carried by an ingredient or potion: the effect plus the base
// magnitude and duration the record gives it (before any skill scaling). A
// duration of 0 is an instant effect; a positive duration is a timed one.
public readonly struct MagicEffectInstance(MagicEffect effect, float magnitude, int duration)
{
    public MagicEffect Effect { get; } = effect;
    public float Magnitude { get; } = magnitude;
    public int Duration { get; } = duration;
}
