using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.IO.Ports;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;
using System.Xml.Serialization;

namespace Type2DkGui
{
    public sealed class Preferences
    {
        public string Programmer = "";
        public string Port = "";
        public int Baud = 1000000;
        public string BinFolder = "";
    }

    internal sealed class MainForm : Form
    {
        private readonly TextBox programmer = new TextBox { ReadOnly = true, Dock = DockStyle.Fill };
        private readonly TextBox bin = new TextBox { ReadOnly = true, Dock = DockStyle.Fill };
        private readonly ComboBox port = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 150 };
        private readonly ComboBox baud = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 180 };
        private readonly Button chooseProgrammer = new Button { Text = "参照…", AutoSize = true };
        private readonly Button chooseBin = new Button { Text = "BINを選ぶ…", AutoSize = true };
        private readonly Button refresh = new Button { Text = "COM一覧を更新", AutoSize = true };
        private readonly Button flash = new Button { Text = "書き込み・照合", AutoSize = true, Height = 42, BackColor = Color.FromArgb(24, 94, 168), ForeColor = Color.White, FlatStyle = FlatStyle.Flat };
        private readonly Button stop = new Button { Text = "強制停止", AutoSize = true, Enabled = false };
        private readonly Button saveLog = new Button { Text = "ログを保存", AutoSize = true };
        private readonly Label state = new Label { AutoSize = true, Text = "BINファイルを選択してください。", Margin = new Padding(0, 10, 0, 10) };
        private readonly Label details = new Label { AutoSize = true, Text = "19番・21番・22番など、接続した基板に合うBINを選びます。" };
        private readonly ProgressBar progress = new ProgressBar { Dock = DockStyle.Fill, Height = 12, Style = ProgressBarStyle.Blocks };
        private readonly TextBox log = new TextBox { Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Both, WordWrap = false, Dock = DockStyle.Fill, Font = new Font("Consolas", 10), BackColor = Color.FromArgb(246, 248, 250) };
        private readonly ConcurrentQueue<string> lines = new ConcurrentQueue<string>();
        private readonly System.Windows.Forms.Timer timer = new System.Windows.Forms.Timer { Interval = 100 };
        private readonly Stopwatch elapsed = new Stopwatch();
        private readonly string settingsPath = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Type2DK-Programmer", "settings.xml");
        private Preferences preferences = new Preferences();
        private CancellationTokenSource cancellation;
        private bool busy;

        internal MainForm()
        {
            Text = "Type2DK Programmer — BIN書き込み 1.0.0";
            Font = new Font("Yu Gothic UI", 10);
            AutoScaleMode = AutoScaleMode.Dpi;
            ClientSize = new Size(850, 670);
            MinimumSize = new Size(720, 600);
            StartPosition = FormStartPosition.CenterScreen;
            var layout = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(22), ColumnCount = 3, RowCount = 12 };
            layout.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 145));
            layout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            layout.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            for (int i = 0; i < 11; i++) layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            Controls.Add(layout);
            AddWide(layout, new Label { Text = "Type2DKにBINを書き込む", AutoSize = true, Font = new Font(Font.FontFamily, 18, FontStyle.Bold), Margin = new Padding(0, 0, 0, 6) }, 0);
            AddWide(layout, new Label { Text = "普段使えているDK6Programmerを指定して、任意のBINを書き込みます。", AutoSize = true, Margin = new Padding(0, 0, 0, 20) }, 1);
            AddRow(layout, "DK6Programmer", programmer, chooseProgrammer, 2);
            var ports = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Fill, WrapContents = false };
            ports.Controls.Add(port); ports.Controls.Add(refresh);
            AddRow(layout, "接続先のCOM", ports, null, 3);
            baud.Items.AddRange(new object[] { "1000000 bps（通常）", "115200 bps（低速）" });
            AddRow(layout, "書き込み速度", baud, null, 4);
            AddRow(layout, "BINファイル", bin, chooseBin, 5);
            details.Margin = new Padding(0, 4, 0, 8);
            AddWide(layout, details, 6);
            AddWide(layout, new Label { AutoSize = true, Text = "選択したCOMのFLASHアプリを書き換えます。シリアルモニターを閉じてから実行してください。\r\n書き込み中はUSBを抜かず、完了まで待ってください。", Margin = new Padding(0, 4, 0, 12) }, 7);
            var buttons = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Fill, WrapContents = false };
            buttons.Controls.Add(flash); buttons.Controls.Add(stop); buttons.Controls.Add(saveLog);
            AddWide(layout, buttons, 8);
            AddWide(layout, state, 9);
            AddWide(layout, progress, 10);
            AddWide(layout, log, 11);
            chooseProgrammer.Click += ChooseProgrammer;
            chooseBin.Click += ChooseBin;
            refresh.Click += delegate { RefreshPorts(); };
            flash.Click += async delegate { await Flash(); };
            stop.Click += delegate
            {
                if (busy && MessageBox.Show(this, "途中で停止すると書き込みが未完了になります。再書き込みが必要です。\r\n強制停止しますか？", "書き込みを強制停止", MessageBoxButtons.YesNo, MessageBoxIcon.Warning, MessageBoxDefaultButton.Button2) == DialogResult.Yes)
                {
                    stop.Enabled = false;
                    cancellation.Cancel();
                }
            };
            saveLog.Click += SaveLog;
            timer.Tick += delegate { DrainLines(); if (busy) state.Text = "書き込み・照合中… " + (int)elapsed.Elapsed.TotalSeconds + " 秒"; };
            FormClosing += delegate(object sender, FormClosingEventArgs e)
            {
                if (busy)
                {
                    e.Cancel = true;
                    MessageBox.Show(this, "書き込み・照合中です。完了まで待ってください。\r\n処理が戻らない場合は「強制停止」を使えます。", Text);
                }
                else SavePreferences();
            };
            LoadPreferences();
            RefreshPorts();
            timer.Start();
        }

        private static void AddWide(TableLayoutPanel panel, Control control, int row)
        {
            panel.Controls.Add(control, 0, row); panel.SetColumnSpan(control, 3);
        }

        private static void AddRow(TableLayoutPanel panel, string title, Control value, Control button, int row)
        {
            panel.Controls.Add(new Label { Text = title, AutoSize = true, Margin = new Padding(0, 8, 0, 8) }, 0, row);
            value.Margin = new Padding(0, 4, 10, 8);
            panel.Controls.Add(value, 1, row);
            if (button != null) panel.Controls.Add(button, 2, row);
            else panel.SetColumnSpan(value, 2);
        }

        private void ChooseProgrammer(object sender, EventArgs e)
        {
            using (var dialog = new OpenFileDialog { Title = "普段使用しているDK6Programmer.exeを選択", Filter = "DK6Programmer.exe|DK6Programmer.exe", CheckFileExists = true })
            {
                if (File.Exists(programmer.Text)) dialog.InitialDirectory = Path.GetDirectoryName(programmer.Text);
                if (dialog.ShowDialog(this) == DialogResult.OK) { programmer.Text = dialog.FileName; SavePreferences(); }
            }
        }

        private void ChooseBin(object sender, EventArgs e)
        {
            using (var dialog = new OpenFileDialog { Title = "接続したType2DKに書き込むBINを選択", Filter = "BINファイル (*.bin)|*.bin", CheckFileExists = true })
            {
                if (Directory.Exists(preferences.BinFolder)) dialog.InitialDirectory = preferences.BinFolder;
                if (dialog.ShowDialog(this) != DialogResult.OK) return;
                bin.Text = dialog.FileName;
                preferences.BinFolder = Path.GetDirectoryName(dialog.FileName);
                try { details.Text = Path.GetFileName(dialog.FileName) + "  /  " + new FileInfo(dialog.FileName).Length.ToString("N0") + " bytes"; }
                catch (IOException) { details.Text = "ファイルを読み取れません。もう一度選択してください。"; }
                state.Text = "COMとBINを確認して「書き込み・照合」を押してください。";
                state.ForeColor = SystemColors.ControlText;
                SavePreferences();
            }
        }

        private void RefreshPorts()
        {
            string previous = port.SelectedItem as string ?? preferences.Port;
            try
            {
                var names = SerialPort.GetPortNames().Distinct(StringComparer.OrdinalIgnoreCase).OrderBy(n => n.Length).ThenBy(n => n).ToArray();
                port.Items.Clear(); port.Items.AddRange(names);
                if (names.Contains(previous)) port.SelectedItem = previous;
                else if (names.Length == 1) port.SelectedIndex = 0;
                if (names.Length == 0) state.Text = "COMポートがありません。基板をUSB接続して一覧を更新してください。";
            }
            catch (Exception ex) { state.Text = "COM一覧の取得に失敗: " + ex.Message; }
        }

        private async Task Flash()
        {
            if (busy) return;
            var request = new FlashRequest { Programmer = programmer.Text, Port = port.SelectedItem as string, Baud = baud.SelectedIndex == 1 ? 115200 : 1000000 };
            string source = bin.Text;
            try { request.Validate(); if (!SerialPort.GetPortNames().Contains(request.Port)) throw new InvalidOperationException("選択したCOMポートが見つかりません。一覧を更新してください。"); }
            catch (Exception ex) { MessageBox.Show(this, ex.Message, "設定を確認", MessageBoxButtons.OK, MessageBoxIcon.Information); return; }
            SavePreferences();
            SetBusy(true);
            log.Clear();
            cancellation = new CancellationTokenSource();
            elapsed.Restart();
            try
            {
                // File copying and programmer execution run off the UI thread.
                FlashResult result = await Task.Run(delegate
                {
                    using (var firmware = new FirmwareSnapshot(source))
                    {
                        lines.Enqueue("Type2DK Programmer GUI 1.0.0  " + DateTimeOffset.Now.ToString("o"));
                        lines.Enqueue("Programmer: " + request.Programmer);
                        lines.Enqueue("BIN: " + source);
                        lines.Enqueue("COM: " + request.Port + "  Baud: " + request.Baud + "  Memory: FLASH  Verify: ON");
                        lines.Enqueue("Bytes: " + firmware.Length + "  SHA256: " + firmware.Hash);
                        lines.Enqueue("--- DK6Programmer ---");
                        return ProgrammerRunner.Run(request, firmware, lines.Enqueue, cancellation.Token);
                    }
                });
                DrainLines();
                log.AppendText("\r\nExit code: " + result.ExitCode + "\r\n");
                if (result.Success)
                {
                    state.Text = "完了：書き込み・読み出し照合に成功しました。";
                    state.ForeColor = Color.DarkGreen;
                }
                else
                {
                    state.Text = result.Cancelled ? "停止しました。書き込みは未完了です。再書き込みしてください。" : "書き込み・照合の成功を確認できません。ログを確認してください。";
                    state.ForeColor = Color.Firebrick;
                    log.AppendText("\r\nCOM番号、USB接続、シリアルモニターの終了、DK6フォルダ内のDLLを確認してください。\r\n");
                }
            }
            catch (OperationCanceledException) { state.Text = "停止しました。"; state.ForeColor = Color.Firebrick; }
            catch (Exception ex)
            {
                DrainLines(); log.AppendText("\r\n" + ex + "\r\n");
                state.Text = "実行できませんでした。ログを確認してください。";
                state.ForeColor = Color.Firebrick;
                MessageBox.Show(this, ex.Message, "実行エラー", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            finally
            {
                elapsed.Stop(); SetBusy(false); cancellation.Dispose(); cancellation = null;
            }
        }

        private void SetBusy(bool value)
        {
            busy = value;
            foreach (Control c in new Control[] { chooseProgrammer, chooseBin, port, baud, refresh, flash, saveLog }) c.Enabled = !value;
            stop.Enabled = value;
            progress.Style = value ? ProgressBarStyle.Marquee : ProgressBarStyle.Blocks;
            if (value) { state.Text = "準備中…"; state.ForeColor = SystemColors.ControlText; }
        }

        private void DrainLines()
        {
            string line;
            var batch = new StringBuilder();
            while (lines.TryDequeue(out line)) batch.AppendLine(line);
            if (batch.Length != 0) log.AppendText(batch.ToString());
        }

        private void SaveLog(object sender, EventArgs e)
        {
            using (var dialog = new SaveFileDialog { Filter = "ログ (*.txt)|*.txt", FileName = "type2dk-flash-" + DateTime.Now.ToString("yyyyMMdd-HHmmss") + ".txt" })
            {
                if (dialog.ShowDialog(this) != DialogResult.OK) return;
                try { File.WriteAllText(dialog.FileName, log.Text, new UTF8Encoding(true)); }
                catch (Exception ex) { MessageBox.Show(this, ex.Message, "ログを保存できませんでした"); }
            }
        }

        private void LoadPreferences()
        {
            try
            {
                using (var stream = File.OpenRead(settingsPath)) preferences = (Preferences)new XmlSerializer(typeof(Preferences)).Deserialize(stream);
            }
            catch (Exception) { preferences = new Preferences(); }
            string[] candidates = { preferences.Programmer, Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "DK6Programmer.exe"), @"C:\NXP\DK6ProductionFlashProgrammer\DK6Programmer.exe" };
            programmer.Text = candidates.FirstOrDefault(File.Exists) ?? "";
            baud.SelectedIndex = preferences.Baud == 115200 ? 1 : 0;
        }

        private void SavePreferences()
        {
            preferences.Programmer = programmer.Text;
            preferences.Port = port.SelectedItem as string ?? "";
            preferences.Baud = baud.SelectedIndex == 1 ? 115200 : 1000000;
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(settingsPath));
                using (var stream = File.Create(settingsPath)) new XmlSerializer(typeof(Preferences)).Serialize(stream, preferences);
            }
            catch (Exception) { /* Preferences are optional; flashing does not depend on saving them. */ }
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing) timer.Dispose();
            base.Dispose(disposing);
        }

        internal void SavePreview(string path)
        {
            Show(); Application.DoEvents();
            using (var bitmap = new Bitmap(Width, Height)) { DrawToBitmap(bitmap, new Rectangle(Point.Empty, Size)); bitmap.Save(path); }
            Close();
        }
    }

    internal static class Program
    {
        [STAThread]
        private static void Main(string[] args)
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            using (var form = new MainForm())
            {
                if (args.Length == 2 && args[0] == "--preview") form.SavePreview(args[1]);
                else Application.Run(form);
            }
        }
    }
}
