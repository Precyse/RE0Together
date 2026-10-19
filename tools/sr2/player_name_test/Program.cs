using CoopLauncher;

var failures = 0;

void Check(bool ok, string what)
{
    Console.WriteLine($"{(ok ? "ok  " : "FAIL")} {what}");
    if (!ok) failures++;
}

LobbyMember Member(ulong id, string name) => new(id, name);

string Local(ulong localId, params LobbyMember[] members) => PlayerName.ForLocal(localId, members);

Check(PlayerName.Shape("Plain Name") == "Plain Name", "ASCII is kept");
Check(PlayerName.Shape("Tést ÿ") == "Tést ÿ", "Latin-1 letters are kept");
Check(PlayerName.Shape("日本") == "??", "CJK becomes ? per code point");
Check(PlayerName.Shape("hi \U0001F600!") == "hi ?!", "an emoji (a surrogate pair) becomes one ?");
Check(PlayerName.Shape("a\u0007b\tc\u0085d") == "abcd", "control characters are dropped");
Check(PlayerName.Shape("a b") == "a?b", "the no-break space becomes ?");
Check(PlayerName.Shape("   padded  ") == "padded", "edges are trimmed");
Check(PlayerName.Shape("\u0001\u0002") == "", "only control characters leave an empty name");
Check(PlayerName.Shape(new string('x', 15)).Length == 15, "15 characters stay");
Check(PlayerName.Shape(new string('x', 16)) == new string('x', 15), "16 characters are cut to 15");
Check(PlayerName.Shape("abcdefghijklmn op") == "abcdefghijklmn", "a cut that ends on a space is trimmed");

Check(Local(1, Member(1, "Alice")) == "Alice", "one member keeps its name");
Check(Local(1, Member(2, "Bob")) == "", "a local member missing from the lobby gets no name line");
Check(Local(1, Member(1, "\u0001")) == "", "a name that shapes to nothing gets no name line");
Check(Local(1, Member(1, "Alice"), Member(2, "Bob")) == "Alice", "different names stay as they are");

var twoEqual = new[] { Member(20, "Sam"), Member(10, "sam") };
Check(Local(10, twoEqual) == "sam" && Local(20, twoEqual) == "Sam 2", "two equal names (any case): the lower id keeps it, the other gets 2");
var threeEqual = new[] { Member(30, "Sam"), Member(10, "Sam"), Member(20, "Sam") };
Check(Local(10, threeEqual) == "Sam" && Local(20, threeEqual) == "Sam 2" && Local(30, threeEqual) == "Sam 3", "three equal names get nothing, 2 and 3 in id order");
var viaShape = new[] { Member(1, "Zoé日"), Member(2, "Zoé?") };
Check(Local(2, viaShape) == "Zoé? 2", "names that are equal after shaping count as equal");

var longEqual = new[] { Member(1, new string('x', 15)), Member(2, new string('x', 15)), Member(3, new string('x', 15)) };
Check(Local(1, longEqual) == new string('x', 15), "the first of three 15 character names is whole");
Check(Local(2, longEqual) == new string('x', 13) + " 2" && Local(2, longEqual).Length == PlayerName.MaxLength, "a suffix keeps the name within 15");
Check(Local(1, new[] { Member(1, "abcdefghijkl mn"), Member(2, "abcdefghijkl mn") }) == "abcdefghijkl mn", "the lower id of two long names is whole");
Check(Local(2, new[] { Member(1, "abcdefghijkl mn"), Member(2, "abcdefghijkl mn") }) == "abcdefghijkl 2", "the base is cut and trimmed before the suffix");

return failures == 0 ? 0 : 1;
