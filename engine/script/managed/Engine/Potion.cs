using System.Collections.Generic;

namespace Recreation;

// A potion, food or poison (ALCH record): an item that applies its magic effects
// when consumed. Reading the effects is the engine's job; applying them is soft
// logic here, built on the actor-value calls and the Effects scheduler so it
// works with no engine support beyond the record data.
public sealed class Potion : Form
{
    private Potion(ulong handle) : base(handle) { }

    public static new Potion From(ulong handle) => new(handle);
    public static Potion From(Form form) => new(form.Handle);

    // The effects this consumable carries, read from its record.
    public IReadOnlyList<MagicEffectInstance> Effects
    {
        get
        {
            int count = Call("GetMagicEffectCount").AsInt();
            var result = new MagicEffectInstance[count];
            for (int i = 0; i < count; i++)
            {
                var effect = MagicEffect.From(Call("GetNthMagicEffectId", i).AsHandle());
                float magnitude = Call("GetNthMagicEffectMagnitude", i).AsFloat();
                int duration = Call("GetNthMagicEffectDuration", i).AsInt();
                result[i] = new MagicEffectInstance(effect, magnitude, duration);
            }
            return result;
        }
    }

    // Applies every effect to `drinker`: an instant effect (duration 0) restores
    // or damages the value at once; a timed one fortifies or saps it for its
    // duration through the Effects scheduler. Effects on values the engine does
    // not model are skipped. Returns how many were applied.
    public int Consume(Actor drinker)
    {
        int applied = 0;
        foreach (MagicEffectInstance e in Effects)
        {
            string actorValue = e.Effect.ActorValue;
            if (actorValue.Length == 0) continue;
            bool harmful = e.Effect.IsDetrimental;
            if (e.Duration <= 0)
            {
                if (harmful) drinker.DamageValue(actorValue, e.Magnitude);
                else drinker.RestoreValue(actorValue, e.Magnitude);
            }
            else
            {
                Modding.Effects.Apply(drinker, actorValue, harmful ? -e.Magnitude : e.Magnitude,
                                      e.Duration);
            }
            applied++;
        }
        return applied;
    }
}
