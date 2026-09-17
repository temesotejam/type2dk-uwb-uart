using System;
using System.IO;
using System.Linq;
using System.Threading;

// CI-only process fixture. Never packaged with the GUI; it does not access a serial port.
internal static class FakeProgrammer
{
    private static int Main(string[] args)
    {
        File.WriteAllText("started.txt", "yes");
        File.WriteAllLines("arguments.txt", args);
        string mode = File.ReadAllText("mode.txt");
        if (mode == "arguments") return 0;
        if (args.Length != 10 || args[0] != "-V" || args[1] != "0" || args[2] != "-P" || args[3] != "1000000" || args[4] != "-s" || args[5] != "COM19" || args[6] != "-Y" || args[7] != "-v" || args[8] != "-p" || !args[9].StartsWith("FLASH=")) return 30;
        if (!File.ReadAllBytes(args[9].Substring(6)).SequenceEqual(new byte[] { 1, 2, 3, 4, 5 })) return 31;
        if (mode == "hang") { Thread.Sleep(30000); return 32; }
        if (mode == "flood")
        {
            for (int i = 0; i < 2500; i++) { Console.WriteLine("stdout " + i); Console.Error.WriteLine("stderr " + i); }
        }
        Console.WriteLine(" COM19: Memory programmed successfully");
        if (mode != "no-verify") Console.WriteLine(" COM19: Memory verified successfully");
        return mode == "exit-error" ? 7 : 0;
    }
}
