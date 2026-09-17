using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;

namespace Type2DkGui
{
    internal sealed class FlashRequest
    {
        internal string Programmer;
        internal string Port;
        internal int Baud;

        internal void Validate()
        {
            if (!File.Exists(Programmer) || !String.Equals(Path.GetFileName(Programmer), "DK6Programmer.exe", StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("DK6Programmer.exe を選択してください。");
            string folder = Path.GetDirectoryName(Path.GetFullPath(Programmer));
            string[] missing = new[] { "programmer.dll", "pdcurses.dll", "ftd2xx.dll", "libgcc_s_dw2-1.dll" }.Where(n => !File.Exists(Path.Combine(folder, n))).ToArray();
            if (missing.Length != 0)
                throw new InvalidOperationException("必要なファイルが同じフォルダにありません: " + String.Join(", ", missing) + "\r\n普段使えているDK6Programmerのフォルダを指定してください。");
            if (!Regex.IsMatch(Port ?? "", @"\ACOM[1-9][0-9]*\z", RegexOptions.IgnoreCase))
                throw new InvalidOperationException("接続した基板のCOMポートを選択してください。");
            if (Baud != 115200 && Baud != 1000000)
                throw new InvalidOperationException("書き込み速度が不正です。");
        }

        internal string Arguments(string snapshotPath)
        {
            // Explicit FLASH prevents a filename from being interpreted as another memory selector.
            return String.Join(" ", new[] { "-V", "0", "-P", Baud.ToString(), "-s", Port.ToUpperInvariant(), "-Y", "-v", "-p", "FLASH=" + snapshotPath }.Select(CommandLine.Quote));
        }
    }

    internal static class CommandLine
    {
        // Windows CRT quoting. No cmd.exe, PowerShell, or shell interpretation is involved.
        internal static string Quote(string value)
        {
            var result = new StringBuilder("\"");
            int slashes = 0;
            foreach (char c in value)
            {
                if (c == '\\') { slashes++; continue; }
                if (c == '"') result.Append('\\', slashes * 2 + 1);
                else result.Append('\\', slashes);
                result.Append(c);
                slashes = 0;
            }
            result.Append('\\', slashes * 2);
            return result.Append('"').ToString();
        }
    }

    internal sealed class FirmwareSnapshot : IDisposable
    {
        internal readonly string PathName;
        internal readonly string Hash;
        internal readonly long Length;
        private readonly string directory;

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern uint GetShortPathName(string path, StringBuilder shortPath, uint size);

        internal FirmwareSnapshot(string source)
        {
            if (!File.Exists(source) || !String.Equals(Path.GetExtension(source), ".bin", StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("書き込む .bin ファイルを選択してください。");
            directory = Path.Combine(Path.GetTempPath(), "Type2DK-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(directory);
            try
            {
                PathName = Path.Combine(directory, "firmware.bin");
                // A private copy fixes the bytes for the whole operation, even if the source is edited later.
                using (var input = new FileStream(source, FileMode.Open, FileAccess.Read, FileShare.Read))
                using (var output = new FileStream(PathName, FileMode.CreateNew, FileAccess.Write, FileShare.None))
                {
                    if (input.Length == 0) throw new InvalidOperationException("BINファイルが空です。");
                    input.CopyTo(output);
                    Length = output.Length;
                }
                using (var sha = SHA256.Create())
                using (var input = File.OpenRead(PathName))
                    Hash = BitConverter.ToString(sha.ComputeHash(input)).Replace("-", "").ToLowerInvariant();

                var shortPath = new StringBuilder(32768);
                uint count = GetShortPathName(PathName, shortPath, (uint)shortPath.Capacity);
                if (count > 0 && count < shortPath.Capacity) PathName = shortPath.ToString();
                // The legacy programmer uses narrow filenames; fail before flashing if Windows cannot encode one.
                if (Encoding.Default.GetString(Encoding.Default.GetBytes(PathName)) != PathName)
                    throw new InvalidOperationException("一時フォルダのパスをDK6Programmerに渡せません。Windowsのユーザーフォルダ名に特殊文字が含まれていないか確認してください。");
            }
            catch { Dispose(); throw; }
        }

        public void Dispose()
        {
            try { if (Directory.Exists(directory)) Directory.Delete(directory, true); }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
        }
    }

    internal sealed class FlashResult
    {
        internal int ExitCode;
        internal bool Cancelled;
        internal string Output;
        internal bool Success
        {
            get
            {
                return !Cancelled && ExitCode == 0
                    && Regex.IsMatch(Output, @"(?im)^\s*(?:COM\d+\s*:\s*)?Memory programmed successfully\s*$")
                    && Regex.IsMatch(Output, @"(?im)^\s*(?:COM\d+\s*:\s*)?Memory verified successfully\s*$");
            }
        }
    }

    internal static class ProgrammerRunner
    {
        internal static FlashResult Run(FlashRequest request, FirmwareSnapshot firmware, Action<string> onLine, CancellationToken cancellation)
        {
            request.Validate();
            cancellation.ThrowIfCancellationRequested();
            var output = new StringBuilder();
            var sync = new object();
            Action<string> receive = delegate(string line)
            {
                if (line == null) return;
                lock (sync)
                {
                    output.AppendLine(line);
                    if (onLine != null) onLine(line);
                }
            };
            using (var process = new Process())
            {
                process.StartInfo = new ProcessStartInfo
                {
                    FileName = Path.GetFullPath(request.Programmer),
                    Arguments = request.Arguments(firmware.PathName),
                    WorkingDirectory = Path.GetDirectoryName(Path.GetFullPath(request.Programmer)),
                    UseShellExecute = false,
                    CreateNoWindow = true,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                    RedirectStandardInput = true,
                    StandardOutputEncoding = Encoding.Default,
                    StandardErrorEncoding = Encoding.Default
                };
                process.OutputDataReceived += delegate(object sender, DataReceivedEventArgs e) { receive(e.Data); };
                process.ErrorDataReceived += delegate(object sender, DataReceivedEventArgs e) { receive(e.Data); };
                process.Start();
                process.StandardInput.Close();
                process.BeginOutputReadLine();
                process.BeginErrorReadLine();
                bool cancelled = false;
                while (!process.WaitForExit(100))
                {
                    if (!cancellation.IsCancellationRequested) continue;
                    cancelled = true;
                    try { process.Kill(); }
                    catch (InvalidOperationException) { }
                    break;
                }
                // Also wait for both asynchronous output readers to drain before interpreting success.
                process.WaitForExit();
                return new FlashResult { ExitCode = process.ExitCode, Cancelled = cancelled || cancellation.IsCancellationRequested, Output = output.ToString() };
            }
        }
    }
}
