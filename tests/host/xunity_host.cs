using System;
using System.IO;
using System.Runtime.InteropServices;

internal static class XUnityHost
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate bool InitializeEx(string username, string path);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    private delegate IntPtr TranslateMMNTW(int data0, [MarshalAs(UnmanagedType.LPWStr)] string text);

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private delegate void StopTranslation();

    [DllImport("kernel32.dll")]
    private static extern IntPtr LoadLibrary(string path);

    [DllImport("kernel32.dll", CharSet = CharSet.Ansi, ExactSpelling = true)]
    private static extern IntPtr GetProcAddress(IntPtr module, string name);

    [DllImport("kernel32.dll")]
    private static extern bool FreeLibrary(IntPtr module);

    private static T Export<T>(IntPtr module, string name) where T : class
    {
        IntPtr address = GetProcAddress(module, name);
        if (address == IntPtr.Zero) throw new EntryPointNotFoundException(name);
        return (T)(object)Marshal.GetDelegateForFunctionPointer(address, typeof(T));
    }

    private static int Run(string libraryPath)
    {
        string datPath = Path.Combine(Environment.CurrentDirectory, @"missing\Dat");
        if (Directory.Exists(datPath) || Directory.Exists("dicts"))
            throw new InvalidOperationException("The working directory must contain no dictionaries.");
        Environment.SetEnvironmentVariable("HDTRANS_DICTS", "missing-dictionary-override");
        IntPtr module = LoadLibrary(libraryPath);
        if (module == IntPtr.Zero) throw new InvalidOperationException("LoadLibrary failed.");
        StopTranslation stop = Export<StopTranslation>(module, "J2K_StopTranslation");
        TranslateMMNTW translate = Export<TranslateMMNTW>(module, "J2K_TranslateMMNTW");
        InitializeEx initialize = Export<InitializeEx>(module, "J2K_InitializeEx");
        try
        {
            if (!initialize("CSUSER123455", datPath))
                throw new InvalidOperationException("InitializeEx failed with an unused Dat path.");
            string[] inputs = { "こんにちは", "日本語" };
            string[] expected = { "안녕하세요", "일본어" };
            for (int round = 0; round < 16; ++round)
            {
                for (int index = 0; index < inputs.Length; ++index)
                {
                    string result = Marshal.PtrToStringAuto(translate(0, inputs[index]));
                    if (result != expected[index])
                        throw new InvalidOperationException("Wrong UTF-16 result: " + result);
                }
            }
            Console.WriteLine("PASS Cdecl delegates, InitializeEx(key, missing Dat), UTF-16 results");
            Console.WriteLine("No J2K_FreeMem call; returned buffers remain allocated");
        }
        finally
        {
            stop();
            if (!FreeLibrary(module)) throw new InvalidOperationException("FreeLibrary failed.");
        }
        Console.WriteLine("PASS StopTranslation() then FreeLibrary, without Terminate");
        return 0;
    }

    private static int Main(string[] args)
    {
        if (args.Length != 1)
        {
            Console.WriteLine("usage: xunity_host <J2KEngine.dll>");
            return 2;
        }
        try { return Run(args[0]); }
        catch (Exception error)
        {
            Console.WriteLine("FAIL " + error);
            return 1;
        }
    }
}
