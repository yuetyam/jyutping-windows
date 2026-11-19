using System.Diagnostics;
using System.Globalization;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.UI;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.Graphics;
using Windows.Storage.Pickers;

namespace JyutpingSettings;

internal sealed class SettingsWindow : Window
{
    private readonly NavigationView _navigation = new()
    {
        IsBackButtonVisible = NavigationViewBackButtonVisible.Collapsed,
        IsSettingsVisible = false,
        OpenPaneLength = 210
    };
    private readonly StackPanel _content = new() { Spacing = 20, MaxWidth = 720 };
    private readonly InfoBar _connection = new() { Severity = InfoBarSeverity.Warning, IsClosable = false };
    private readonly InfoBar _operationStatus = new() { IsClosable = true };
    private readonly Dictionary<int, ComboBox> _controls = [];
    private readonly SemaphoreSlim _rpc = new(1);
    private readonly Microsoft.UI.Dispatching.DispatcherQueueTimer _refreshTimer;

    private uint[]? _settings;
    private StackPanel? _preview;
    private string[] _previewLines = [];
    private bool _updating;
    private bool _memoryAvailable;
    private bool _transferAvailable;
    private Process? _transfer;
    private Button? _importMemory;
    private Button? _exportMemory;
    private Button? _cancelTransfer;
    private Button? _clearMemory;

    internal SettingsWindow()
    {
        Title = Texts.Get("Jyutping Settings");
        AppWindow.Resize(new SizeInt32(920, 660));
        SystemBackdrop = new MicaBackdrop();
        Content = _navigation;

        AddPage("General", Symbol.Setting);
        AddPage("Appearance", Symbol.Preview);
        AddPage("Reverse Lookup", Symbol.Find);
        AddPage("Tones", Symbol.Audio);
        AddPage("Fonts", Symbol.Font);
        AddPage("Input Memory", Symbol.Library);
        AddPage("Hotkeys", Symbol.Keyboard);
        AddPage("About", Symbol.Help);
        _navigation.SelectionChanged += (_, args) =>
        {
            if (args.SelectedItem is NavigationViewItem { Tag: string page })
            {
                ShowPage(page);
            }
        };

        var body = new StackPanel { Spacing = 20, Padding = new Thickness(32, 24, 32, 32) };
        body.Children.Add(_connection);
        body.Children.Add(_operationStatus);
        body.Children.Add(_content);
        _navigation.Content = new ScrollViewer { Content = body, HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled };
        _connection.Message = Texts.Get("Connecting to Jyutping");
        _connection.IsOpen = true;
        _navigation.SelectedItem = _navigation.MenuItems[0];

        _refreshTimer = DispatcherQueue.CreateTimer();
        _refreshTimer.Interval = TimeSpan.FromSeconds(1);
        _refreshTimer.Tick += async (_, _) => await Refresh();
        Activated += async (_, _) =>
        {
            if (!_refreshTimer.IsRunning)
            {
                _refreshTimer.Start();
                await Refresh();
            }
        };
        Closed += (_, _) =>
        {
            _refreshTimer.Stop();
            CancelTransfer();
        };
    }

    private void AddPage(string page, Symbol icon)
    {
        _navigation.MenuItems.Add(new NavigationViewItem { Content = Texts.Get(page), Tag = page, Icon = new SymbolIcon(icon) });
    }

    private void ShowPage(string page)
    {
        _controls.Clear();
        _preview = null;
        _clearMemory = null;
        _importMemory = null;
        _exportMemory = null;
        _cancelTransfer = null;
        _content.Children.Clear();
        _content.Children.Add(new TextBlock
        {
            Text = Texts.Get(page),
            FontSize = 28,
            FontWeight = Microsoft.UI.Text.FontWeights.SemiBold,
            TextWrapping = TextWrapping.Wrap
        });

        switch (page)
        {
            case "General":
                AddSetting(0, "Default input mode", 1, ["Cantonese", "ABC"]);
                AddSetting(1, "Character width", 1, ["Half width", "Full width"]);
                AddSetting(2, "Punctuation", 1, ["Cantonese", "English"]);
                AddSetting(3, "Character variant", 1, ["Traditional", "Hong Kong", "Taiwan", "Simplified"]);
                AddNumberSetting(4, "Candidates per page", 1, 10);
                break;
            case "Appearance":
                AddSetting(3, "Character variant", 1, ["Traditional", "Hong Kong", "Taiwan", "Simplified"]);
                AddNumberSetting(4, "Candidates per page", 1, 10);
                AddPreview();
                break;
            case "Reverse Lookup":
                AddText("Type the prefix followed by the lookup code.");
                AddText("r   Pinyin");
                AddText("v   Cangjie");
                AddText("x   Stroke");
                AddText("q   Character structure");
                break;
            case "Tones":
                AddText("Use v, x, and q for tones 1, 2, and 3. Double the letter for tones 4, 5, and 6.");
                AddText("v  1       x  2       q  3\nvv  4     xx  5     qq  6");
                break;
            case "Fonts":
                AddNumberSetting(5, "Candidate size", 11, 24);
                AddNumberSetting(6, "Number size", 11, 24);
                AddNumberSetting(7, "Jyutping size", 11, 24);
                AddPreview();
                break;
            case "Input Memory":
                _clearMemory = new Button { Content = Texts.Get("Clear learned words"), IsEnabled = _memoryAvailable };
                _clearMemory.Click += async (_, _) => await ClearMemory();
                _importMemory = new Button { Content = Texts.Get("Import learned words") };
                _importMemory.Click += async (_, _) => await TransferMemory(true);
                _exportMemory = new Button { Content = Texts.Get("Export learned words") };
                _exportMemory.Click += async (_, _) => await TransferMemory(false);
                _cancelTransfer = new Button { Content = Texts.Get("Cancel"), Visibility = Visibility.Collapsed };
                _cancelTransfer.Click += (_, _) => CancelTransfer();
                _content.Children.Add(_importMemory);
                _content.Children.Add(_exportMemory);
                _content.Children.Add(_clearMemory);
                _content.Children.Add(_cancelTransfer);
                break;
            case "Hotkeys":
                AddText("Space selects the highlighted candidate.");
                AddText("Enter commits the typed text.");
                AddText("Escape cancels the composition.");
                break;
            case "About":
                var version = Assembly.GetExecutingAssembly().GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion;
                AddText("Jyutping").Text = $"Jyutping {version}";
                _content.Children.Add(new HyperlinkButton { Content = Texts.Get("Jyutping website"), NavigateUri = new Uri("https://jyutping.app") });
                var update = new Button { Content = Texts.Get("Check for updates") };
                string updater = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "Updater", "JyutpingUpdater.exe"));
                if (!File.Exists(updater))
                {
                    updater = Path.Combine(AppContext.BaseDirectory, "Updater", "JyutpingUpdater.exe");
                }
                update.IsEnabled = File.Exists(updater);
                update.Click += (_, _) =>
                {
                    try
                    {
                        Process.Start(new ProcessStartInfo(updater, "--check-for-updates") { UseShellExecute = false });
                    }
                    catch (System.ComponentModel.Win32Exception)
                    {
                        ShowOperationStatus(InfoBarSeverity.Error, "The updater could not start.");
                    }
                };
                _content.Children.Add(update);
                break;
        }
        ApplySettings();
    }

    private TextBlock AddText(string text)
    {
        var block = new TextBlock { Text = Texts.Get(text), TextWrapping = TextWrapping.Wrap };
        _content.Children.Add(block);
        return block;
    }

    private void AddNumberSetting(int index, string label, int minimum, int maximum)
    {
        AddSetting(index, label, minimum, Enumerable.Range(minimum, maximum - minimum + 1)
            .Select(number => number.ToString(CultureInfo.CurrentCulture)).ToArray());
    }

    private void AddSetting(int index, string label, int firstValue, string[] choices)
    {
        var control = new ComboBox
        {
            Header = Texts.Get(label),
            ItemsSource = choices.Select(Texts.Get).ToArray(),
            Tag = firstValue,
            MinWidth = 200,
            IsEnabled = false
        };
        AutomationProperties.SetName(control, Texts.Get(label));
        AutomationProperties.SetAutomationId(control, $"Setting{index}");
        _controls.Add(index, control);
        _content.Children.Add(control);
        control.SelectionChanged += async (_, _) =>
        {
            if (!_updating && control.SelectedIndex >= 0)
            {
                await WriteSetting(index, (uint)(firstValue + control.SelectedIndex));
            }
        };
    }

    private void AddPreview()
    {
        AddText("Candidate preview").FontWeight = Microsoft.UI.Text.FontWeights.SemiBold;
        _preview = new StackPanel { Spacing = 10 };
        _content.Children.Add(new Border
        {
            Child = _preview,
            Padding = new Thickness(16),
            CornerRadius = new CornerRadius(8),
            BorderThickness = new Thickness(1),
            BorderBrush = (Brush)Application.Current.Resources["CardStrokeColorDefaultBrush"],
            Background = (Brush)Application.Current.Resources["CardBackgroundFillColorDefaultBrush"]
        });
    }

    private void ApplySettings()
    {
        _updating = true;
        try
        {
            foreach (var (index, control) in _controls)
            {
                control.IsEnabled = _settings is not null;
                control.SelectedIndex = _settings is not null ? (int)_settings[index] - (int)control.Tag : -1;
            }
        }
        finally
        {
            _updating = false;
        }

        if (_preview is null)
        {
            if (_clearMemory is not null)
            {
                _clearMemory.IsEnabled = _memoryAvailable && _transfer is null;
            }
            if (_importMemory is not null && _exportMemory is not null && _cancelTransfer is not null)
            {
                _importMemory.IsEnabled = _transferAvailable && _transfer is null;
                _exportMemory.IsEnabled = _transferAvailable && _transfer is null;
                _cancelTransfer.Visibility = _transfer is null ? Visibility.Collapsed : Visibility.Visible;
            }
            return;
        }
        _preview.Children.Clear();
        if (_settings is null)
        {
            return;
        }
        int indexInPage = 0;
        foreach (var line in _previewLines.Take((int)_settings[4]))
        {
            string[] fields = line.Split('\t', 2);
            if (fields.Length != 2)
            {
                continue;
            }
            var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 14 };
            row.Children.Add(new TextBlock { Text = (++indexInPage).ToString(CultureInfo.CurrentCulture), FontSize = _settings[6], VerticalAlignment = VerticalAlignment.Center });
            row.Children.Add(new TextBlock { Text = fields[0], FontSize = _settings[5], VerticalAlignment = VerticalAlignment.Center });
            row.Children.Add(new TextBlock { Text = fields[1], FontSize = _settings[7], VerticalAlignment = VerticalAlignment.Center });
            _preview.Children.Add(row);
        }
    }

    private async Task Refresh()
    {
        if (!await _rpc.WaitAsync(0))
        {
            return;
        }
        try
        {
            await ReadSettings();
        }
        finally
        {
            _rpc.Release();
        }
    }

    private async Task WriteSetting(int field, uint value)
    {
        await _rpc.WaitAsync();
        try
        {
            var error = await Task.Run(() => Native.Write((uint)field, value));
            await ReadSettings(error);
        }
        finally
        {
            _rpc.Release();
        }
    }

    private async Task ReadSettings(uint writeError = 0)
    {
        uint[] values = new uint[8];
        bool previewRequested = _preview is not null;
        var loaded = await Task.Run(() =>
        {
            uint error = Native.Read(values, (uint)values.Length);
            var preview = new StringBuilder(2048);
            if (error == 0 && previewRequested)
            {
                uint standard = values[3] switch { 2 => 6, 3 => 7, 4 => 51, _ => 1 };
                Native.Preview(standard, preview, (uint)preview.Capacity);
            }
            return (error, preview.ToString(), memoryAvailable: error == 0 && Native.HasInputMemory(), transferAvailable: error == 0 && Native.HasMemoryTransfer());
        });
        _settings = loaded.error == 0 ? values : null;
        _previewLines = loaded.Item2.Split('\n', StringSplitOptions.RemoveEmptyEntries);
        _memoryAvailable = loaded.memoryAvailable;
        _transferAvailable = loaded.transferAvailable;
        _connection.Message = loaded.error != 0 ? Texts.Get("Jyutping settings are unavailable. The app will reconnect.") : Texts.Get("The setting could not be saved.");
        _connection.IsOpen = loaded.error != 0 || writeError != 0;
        ApplySettings();
    }

    private void ShowOperationStatus(InfoBarSeverity severity, string message)
    {
        _operationStatus.Severity = severity;
        _operationStatus.Message = Texts.Get(message);
        _operationStatus.IsOpen = true;
    }

    private async Task TransferMemory(bool importing)
    {
        var handle = WinRT.Interop.WindowNative.GetWindowHandle(this);
        string? path;
        if (importing)
        {
            var picker = new FileOpenPicker();
            picker.FileTypeFilter.Add(".sqlite3");
            WinRT.Interop.InitializeWithWindow.Initialize(picker, handle);
            path = (await picker.PickSingleFileAsync())?.Path;
        }
        else
        {
            string timestamp = DateTime.Now.ToString("yyyyMMdd-HHmmss", CultureInfo.InvariantCulture);
            var picker = new FileSavePicker { SuggestedFileName = $"memory-{timestamp}" };
            picker.FileTypeChoices.Add(Texts.Get("Jyutping memory file"), [".sqlite3"]);
            WinRT.Interop.InitializeWithWindow.Initialize(picker, handle);
            path = (await picker.PickSaveFileAsync())?.Path;
        }
        if (string.IsNullOrEmpty(path) || _transfer is not null)
        {
            return;
        }
        var server = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "JyutpingServer.exe"));
        var start = new ProcessStartInfo(server)
        {
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardInput = true
        };
        start.ArgumentList.Add(importing ? "--import-memory" : "--export-memory");
        start.ArgumentList.Add(path);
        try
        {
            using var process = Process.Start(start) ?? throw new InvalidOperationException();
            _transfer = process;
            ShowOperationStatus(InfoBarSeverity.Informational, importing ? "Importing learned words…" : "Exporting learned words…");
            ApplySettings();
            await process.WaitForExitAsync();
            var (severity, message) = process.ExitCode switch
            {
                0 => (InfoBarSeverity.Success, importing ? "Learned words imported." : "Learned words exported."),
                2 => (InfoBarSeverity.Informational, "Memory transfer cancelled."),
                _ => (InfoBarSeverity.Error, "Memory transfer failed.")
            };
            ShowOperationStatus(severity, message);
        }
        catch (Exception error) when (error is InvalidOperationException or System.ComponentModel.Win32Exception)
        {
            ShowOperationStatus(InfoBarSeverity.Error, "Memory transfer failed.");
        }
        finally
        {
            _transfer = null;
            ApplySettings();
        }
    }

    private void CancelTransfer()
    {
        try
        {
            if (_transfer is { HasExited: false } process)
            {
                process.StandardInput.WriteLine("cancel");
                process.StandardInput.Flush();
            }
        }
        catch (IOException)
        {
        }
    }

    private async Task ClearMemory()
    {
        var confirmation = new ContentDialog
        {
            XamlRoot = _navigation.XamlRoot,
            Title = Texts.Get("Clear learned words?"),
            Content = Texts.Get("This removes the words Jyutping has learned."),
            PrimaryButtonText = Texts.Get("Clear"),
            CloseButtonText = Texts.Get("Cancel"),
            DefaultButton = ContentDialogButton.Close
        };
        if (await confirmation.ShowAsync() != ContentDialogResult.Primary)
        {
            return;
        }
        await _rpc.WaitAsync();
        try
        {
            uint error = await Task.Run(Native.ClearInputMemory);
            ShowOperationStatus(error == 0 ? InfoBarSeverity.Success : InfoBarSeverity.Error, error == 0 ? "Learned words cleared." : "Could not clear learned words.");
        }
        finally
        {
            _rpc.Release();
        }
    }

    private static class Native
    {
        [DllImport("JyutpingSettingsRpc.dll", EntryPoint = "JyutpingReadSettings")]
        internal static extern uint Read([Out] uint[] values, uint count);

        [DllImport("JyutpingSettingsRpc.dll", EntryPoint = "JyutpingWriteSetting")]
        internal static extern uint Write(uint field, uint value);

        [DllImport("JyutpingSettingsRpc.dll", EntryPoint = "JyutpingPreviewCandidates", CharSet = CharSet.Unicode)]
        internal static extern uint Preview(uint standard, StringBuilder text, uint count);

        [DllImport("JyutpingSettingsRpc.dll", EntryPoint = "JyutpingHasInputMemory")]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool HasInputMemory();

        [DllImport("JyutpingSettingsRpc.dll", EntryPoint = "JyutpingHasMemoryTransfer")]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool HasMemoryTransfer();

        [DllImport("JyutpingSettingsRpc.dll", EntryPoint = "JyutpingClearInputMemory")]
        internal static extern uint ClearInputMemory();
    }
}
