using System.Collections.Generic;

namespace Recreation;

// An alchemy ingredient (INGR record): a form carrying up to four magic effects.
// Combining ingredients that share an effect is how potions are brewed; see the
// Skyrim Alchemy system for the rules.
public sealed class Ingredient : Form
{
    private Ingredient(ulong handle) : base(handle) { }

    public static new Ingredient From(ulong handle) => new(handle);

    // The ingredient's magic effects, read from its record. Materialised eagerly
    // because the engine caches the parse between the count and the per-index
    // reads. Empty if the form is not an ingredient.
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
}
