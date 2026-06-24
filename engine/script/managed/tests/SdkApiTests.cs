using Recreation;
using Recreation.Interop;

namespace Recreation.Tests;

// Confirms the thin global-API wrappers dispatch to the right native with the
// right arguments. Catches name and argument-order typos in the wrappers.
public static class SdkApiTests
{
    public static void Run(Check check)
    {
        var fake = new FakeBackend();
        Native.Backend = fake;

        Debug.Notification("hello");
        check.Equal("Debug.Notification type", "Debug", fake.LastGlobalType);
        check.Equal("Debug.Notification function", "Notification", fake.LastGlobalFunction);
        check.Equal("Debug.Notification passes the message", "hello", fake.LastStringArg);

        Debug.Trace("log line");
        check.Equal("Debug.Trace function", "Trace", fake.LastGlobalFunction);

        int r = Utility.RandomInt(7, 9);
        check.Equal("Utility.RandomInt type", "Utility", fake.LastGlobalType);
        check.Equal("Utility.RandomInt returns engine value", 7, r);

        Native.Backend = null;
    }
}
