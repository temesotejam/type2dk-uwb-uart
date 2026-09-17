using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using Type2DkGui;

internal static class Tests
{
    private static int checks;
    private static void Check(bool value, string message)
    {
        if (!value) throw new Exception(message);
        checks++;
    }
    private static void Reject(Action action, string message)
    {
        bool rejected = false;
        try { action(); } catch (InvalidOperationException) { rejected = true; }
        Check(rejected, message);
    }

    private static int Main(string[] args)
    {
        string directory = Path.Combine(Path.GetTempPath(), "DK6 GUI tests 日本語 & space " + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);
        try
        {
            string executable = Path.Combine(directory, "DK6Programmer.exe");
            File.Copy(args[0], executable);
            foreach (string name in new[] { "programmer.dll", "pdcurses.dll", "ftd2xx.dll", "libgcc_s_dw2-1.dll" }) File.WriteAllText(Path.Combine(directory, name), "CI fixture");
            var request = new FlashRequest { Programmer = executable, Port = "COM19", Baud = 1000000 };
            request.Validate();
            request.Port = "COM19 -e";
            Reject(request.Validate, "port injection accepted");
            request.Port = "COM19";
            File.Delete(Path.Combine(directory, "pdcurses.dll"));
            Reject(request.Validate, "missing dependency accepted");
            File.WriteAllText(Path.Combine(directory, "pdcurses.dll"), "CI fixture");
            string file = Path.Combine(directory, "firmware 日本語 & $ ! = sample.bin");
            File.WriteAllBytes(file, new byte[0]);
            Reject(delegate { using (new FirmwareSnapshot(file)) { } }, "empty BIN accepted");
            File.WriteAllBytes(file, new byte[] { 1, 2, 3, 4, 5 });
            string snapshotPath;
            using (var snapshot = new FirmwareSnapshot(file))
            {
                snapshotPath = snapshot.PathName;
                File.WriteAllBytes(file, new byte[] { 99 });
                Check(snapshot.Length == 5 && File.ReadAllBytes(snapshot.PathName).Length == 5, "source modification changed snapshot");
                Check(snapshot.Hash == "74f81fe167d99b4cb41d6d0ccda82278caee9f3e2f25d5e5a3936ff3dcec60d0", "wrong SHA256");
                foreach (string mode in new[] { "success", "no-verify", "exit-error", "flood" })
                {
                    File.WriteAllText(Path.Combine(directory, "mode.txt"), mode);
                    int lines = 0;
                    var result = ProgrammerRunner.Run(request, snapshot, delegate { Interlocked.Increment(ref lines); }, CancellationToken.None);
                    Check(result.Success == (mode == "success" || mode == "flood"), "wrong outcome: " + mode);
                    if (mode == "flood") Check(lines == 5002, "stdout/stderr lost or undrained");
                    Console.WriteLine(mode + ": PASS");
                }
                File.WriteAllText(Path.Combine(directory, "mode.txt"), "hang");
                using (var cancel = new CancellationTokenSource(1000))
                {
                    var watch = Stopwatch.StartNew();
                    var result = ProgrammerRunner.Run(request, snapshot, null, cancel.Token);
                    Check(result.Cancelled && !result.Success && watch.Elapsed.TotalSeconds < 10, "cancellation failed");
                }
                File.Delete(Path.Combine(directory, "started.txt"));
                using (var cancel = new CancellationTokenSource())
                {
                    cancel.Cancel();
                    try { ProgrammerRunner.Run(request, snapshot, null, cancel.Token); throw new Exception("pre-cancel accepted"); }
                    catch (OperationCanceledException) { }
                    Check(!File.Exists(Path.Combine(directory, "started.txt")), "pre-cancel started child");
                }
            }
            Check(!File.Exists(snapshotPath), "snapshot left behind");
            // Exercise Windows argument parsing, including spaces, quotes, and trailing backslashes.
            File.WriteAllText(Path.Combine(directory, "mode.txt"), "arguments");
            string[] values = { "", "space name", "日本語.bin", "a&b|c$()!", "a\"b", @"C:\space folder\", "FLASH=x=y.bin" };
            using (var process = Process.Start(new ProcessStartInfo { FileName = executable, WorkingDirectory = directory, Arguments = String.Join(" ", values.Select(CommandLine.Quote)), UseShellExecute = false, CreateNoWindow = true })) process.WaitForExit();
            Check(File.ReadAllLines(Path.Combine(directory, "arguments.txt")).SequenceEqual(values), "argument roundtrip failed");
            Check(!new FlashResult { ExitCode = 0, Output = "Error: Memory verified successfully" }.Success, "false success accepted");
            Console.WriteLine("PASS: " + checks + " checks; no physical hardware was used.");
            return 0;
        }
        catch (Exception ex) { Console.Error.WriteLine(ex); return 1; }
        finally { Directory.Delete(directory, true); }
    }
}
