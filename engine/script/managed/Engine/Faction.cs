namespace Recreation;

// A faction (FACT record): a group an actor can belong to, with a crime-gold
// ledger and pairwise reactions to other factions. Actor membership lives on
// Actor (GetFactionRank, IsInFaction, ...); this is the faction-wide state.
public class Faction : Form
{
    protected Faction(ulong handle) : base(handle) { }

    public static new Faction From(ulong handle) => new(handle);

    // The reaction toward another faction: -1 enemy, 0 neutral, 1 friend, 2 ally.
    public int GetReaction(Faction other) => Call("GetReaction", other).AsInt();
    public void SetReaction(Faction other, int reaction) => Call("SetReaction", other, reaction);

    public int CrimeGold
    {
        get => Call("GetCrimeGold").AsInt();
        set => Call("SetCrimeGold", value);
    }
    public void ModCrimeGold(int delta) => Call("ModCrimeGold", delta);
}
