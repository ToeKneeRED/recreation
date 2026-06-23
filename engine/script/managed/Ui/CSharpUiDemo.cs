using System;

namespace Recreation;

// A live proof that ultragui's scripting runtime is now C#. The demo panel built
// in game_ui.cc (shown with RECREATION_UI_CSHARP_DEMO=1) routes its button click
// and its checkbox/slider on_change straight into these [UiHandler] methods; each
// reaches back through the Widget API to update the on-screen status, exercising
// the full native<->managed round trip. Harmless when the panel is absent: the
// handlers register but never fire.
internal static class CSharpUiDemo
{
    private static int _clicks;
    private const string Idle = "click / toggle / drag — handlers run in C#";

    [UiHandler]
    private static void on_csdemo_click(Widget w)
    {
        _clicks++;
        Ui.Find("csdemo_status").Text = $"button clicked {_clicks}x — handled in C#";
        Console.WriteLine($"[managed/ui-demo] click #{_clicks} (widget {w.Handle:x})");
        // Show off the managed timer: revert the status after a couple seconds.
        Ui.After(2.5, () => Ui.Find("csdemo_status").Text = Idle);
    }

    [UiHandler]
    private static void on_csdemo_check(Widget w)
    {
        Ui.Find("csdemo_status").Text = w.Checked ? "checkbox ON — read from C#" : "checkbox OFF — read from C#";
        Console.WriteLine($"[managed/ui-demo] checkbox = {w.Checked}");
    }

    [UiHandler]
    private static void on_csdemo_slider(Widget w)
    {
        Ui.Find("csdemo_status").Text = $"slider = {w.Value:F0} — read from C#";
    }
}
