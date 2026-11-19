using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.Windows.AppLifecycle;

namespace JyutpingSettings;

public partial class App : Application
{
    private SettingsWindow? _window;

    public App()
    {
        InitializeComponent();
    }

    [STAThread]
    public static void Main()
    {
        WinRT.ComWrappersSupport.InitializeComWrappers();
        var activation = AppInstance.GetCurrent().GetActivatedEventArgs();
        var instance = AppInstance.FindOrRegisterForKey("Jyutping.Settings");
        if (!instance.IsCurrent)
        {
            Task.Run(async () => await instance.RedirectActivationToAsync(activation)).GetAwaiter().GetResult();
            return;
        }

        Start(_ =>
        {
            var dispatcher = DispatcherQueue.GetForCurrentThread();
            SynchronizationContext.SetSynchronizationContext(new DispatcherQueueSynchronizationContext(dispatcher));
            var app = new App();
            instance.Activated += (_, _) => dispatcher.TryEnqueue(app.ShowSettings);
        });
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        ShowSettings();
    }

    private void ShowSettings()
    {
        _window ??= new SettingsWindow();
        _window.Activate();
        var handle = WinRT.Interop.WindowNative.GetWindowHandle(_window);
        SetForegroundWindow(handle);
    }

    [System.Runtime.InteropServices.DllImport("user32.dll")]
    private static extern bool SetForegroundWindow(nint window);
}
